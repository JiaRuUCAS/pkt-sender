#include "util.h"
#include "probe.h"
#include "port.h"
#include "pktsender.h"
#include "pt_trace.h"

#include <rte_errno.h>
#include <rte_ring.h>
#include <rte_mbuf.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <rte_ethdev.h>

static struct probe_ctl *probe_list = NULL;
static uint8_t nb_probe = 0;

/** TX timer */
static struct rte_timer probe_timer;

/** Probe TX mempool */
static struct rte_mempool *probe_mp = NULL;

/** free all */
void
probe_free(void)
{
	if (probe_list != NULL)
		zfree(probe_list);

	if (probe_mp != NULL) {
		rte_mempool_free(probe_mp);
		probe_mp = NULL;
	}

	LOG_DEBUG("Free probe_list");
}

static uint64_t
__measure_rd_time_cost(uint8_t port)
{
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
	int i = 0;
	uint64_t start_cyc = 0, diff_cyc = 0;
	double diff_ns = 0;

	start_cyc = rte_get_tsc_cycles();
	for (i = 0; i < 1000; i++) {
		rte_eth_timesync_read_time(port, &ts);
	}
	diff_cyc = rte_get_tsc_cycles() - start_cyc;

	diff_ns = diff_cyc / (double)(rte_get_tsc_hz() / 1000000000);

	LOG_DEBUG("Cost of read timestamp: cycle %lu, %lf ns",
					diff_cyc, diff_ns / 1000);

	return (uint64_t)(diff_ns / 1000);
}

/** initialize per-port probe_ctl structure */
static int
__init_local_probe(uint8_t portid, uint8_t queueid)
{
	struct probe_ctl *ctl = &probe_list[portid];

	/* init pkt_configure */
	memcpy(&ctl->pkt_configure, port_get_pkt_seq(portid), sizeof(struct pkt_seq));
	ctl->pkt_configure.proto = PROBE_PKT_PROTO;
	ctl->pkt_configure.pkt_len = PROBE_PKT_LEN;

	ctl->portid = portid;
	ctl->queueid = queueid;
	ctl->next_pkt = NULL;
	ctl->next_idx = 0;

	return 0;
}

/** initialize global probe_ctl list */
int
probe_init(uint8_t nb_ports)
{
   	uint8_t	portid = 0;

	probe_list = (struct probe_ctl *)malloc(
					sizeof(struct probe_ctl) * nb_ports);
	if (probe_list == NULL) {
		LOG_ERROR("Failed to allocate memory for probe_list");
		return ERR_MEMORY;
	}
	memset(probe_list, 0, sizeof(struct probe_ctl) * nb_ports);

	nb_probe = nb_ports;

	for (portid = 0; portid < nb_ports; portid++) {
		if (!port_is_enabled(portid))
			continue;

		if (__init_local_probe(portid, TXQ_LATENCY) < 0) {
			LOG_ERROR("Failed to initialize probe_ctl for port %u",
							portid);
			goto fail_free_all;
		}
	}

	/* create mempool */
	probe_mp = rte_pktmbuf_pool_create("probe_mbuf_pool", PROBE_PKT_MAX,
					PROBE_MP_CACHE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, 0);
	if (probe_mp == NULL) {
		LOG_ERROR("Failed to create probe mempool, %s",
						rte_strerror(rte_errno));
		goto fail_free_all;
	}

	LOG_DEBUG("Init probe_list");
	return 0;

fail_free_all:
	probe_free();
	return ERR_MEMORY;
}

static int
__construct_probe(struct probe_ctl *ctl)
{
	struct rte_mbuf *pkt = NULL;
//	void *hdr = NULL;
//	struct probe_payload *payload;
	struct pkt_fmt *fmt = NULL;

	pkt = rte_pktmbuf_alloc(probe_mp);
	if (pkt == NULL) {
		LOG_ERROR("Failed to allocate mbuf from probe_mp");
		return ERR_DPDK;
	}
	pkt->pkt_len = PROBE_PKT_LEN;
	pkt->data_len = PROBE_PKT_LEN;

	fmt = rte_pktmbuf_mtod(pkt, struct pkt_fmt *);

	/* fill probe payload */
	fmt->probe_magic = PROBE_PKT_MAGIC;
	fmt->probe_idx = ctl->next_idx;
	fmt->probe_sender = ctl->portid;

	/* set ether_type to PTP */
	fmt->ether_type = rte_cpu_to_be_16(PROBE_PKT_ETHER_TYPE);
	fmt->ptp_msg_id = PROBE_PTP_MSG;
	fmt->ptp_version = PROBE_PTP_VERSION;

	/* set mac address */
	memcpy(fmt->src_mac, ctl->pkt_configure.src_mac.addr_bytes, 6);
	memcpy(fmt->dst_mac, ctl->pkt_configure.dst_mac.addr_bytes, 6);

	/* Enable flag for hardware timestamping. */
	pkt->ol_flags |= PKT_TX_IEEE1588_TMST;
//
//	LOG_DEBUG("construct pkt %lu", payload->probe_idx);

	ctl->next_pkt = pkt;
	ctl->next_idx++;
	return 0;
}

static void
__process_probe(struct rte_timer *timer __rte_unused,
				void *arg __rte_unused)
{
	uint8_t i = 0;
	struct probe_ctl *ctl = NULL;
	int ret = 0;
//	uint64_t cycle = 0;

	if ((pktsender.job_state & (1 << LCORE_JOB_TX)) == 0)
		return;

	/* send probe packets */
	for (i = 0; i < nb_probe; i++) {
		if (!port_is_enabled(i))
			continue;

		ctl = &probe_list[i];

		if (ctl->next_pkt == NULL) {
			if (__construct_probe(ctl) < 0) {
				return;
			}
		}

//		cycle = rte_get_tsc_cycles();

		trace_hw_tx_prepare(ctl->portid, &ctl->next_pkt, 1);

		/* send probe packet */
		ret = rte_eth_tx_burst(ctl->portid, ctl->queueid, &ctl->next_pkt, 1);
		if (ret < 1) {
			LOG_ERROR("Failed to send probe packet to port %u", ctl->portid);
			continue;
		}
		trace_hw_tx_record(ctl->portid, ctl->next_pkt);

//		LOG_DEBUG("Port %u: send probe pkt %lu",
//						ctl->portid, ctl->next_idx - 1);
//		fprintf(ctl->tx_output, "%u\t%lu\t%lu\n",
//						ctl->portid, ctl->next_idx - 1, cycle);
		rte_pktmbuf_free(ctl->next_pkt);
		ctl->next_pkt = NULL;
	}
}

//static inline void
//__recv_probe_pkt(uint8_t portid, struct rte_mbuf *pkt)
//{
//	struct probe_payload *probe = NULL;
//	struct ether_hdr *eth = NULL;
//	struct udpip_hdr *udpip = NULL;
//	uint64_t cycle = 0;
//	struct probe_ctl *ctl = &probe_list[portid];
//
////	struct probe_ctl *ctl = &probe_list[portid];
//	if (pkt->pkt_len != PROBE_PKT_LEN)
//		return;
//
//	/* check for L2 type */
//	eth = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
//	if (eth->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4))
//		return;
//
//	/* check for L3 type */
//	udpip = rte_pktmbuf_mtod_offset(pkt, struct udpip_hdr *,
//					sizeof(struct ether_hdr));
//	if (udpip->ip.next_proto_id != PROBE_PKT_PROTO)
//		return;
//
//	/* check for probe magic value */
//	probe = rte_pktmbuf_mtod_offset(pkt, struct probe_payload *,
//					sizeof(struct ether_hdr) + sizeof(struct udpip_hdr));
//
////	LOG_DEBUG("Recv pkt %lx, port %x, magic %x",
////					probe->probe_idx, probe->probe_sender,
////					probe->probe_magic);
//
//	if (probe->probe_magic != PROBE_PKT_MAGIC)
//		return;
//
//	cycle = rte_get_tsc_cycles();
//
//	if (ctl->rx_output) {
//		fprintf(ctl->rx_output, "%u\t%lu\t%lu\n",
//						probe->probe_sender,
//						probe->probe_idx, cycle);
////		LOG_DEBUG("recv probe pkt %lu", probe->data.probe_idx);
//	}
//}

/* receive probe packet */
void
probe_receive(uint8_t portid, struct rte_mbuf *pkts[], uint16_t cnt)
{
	trace_hw_rx_record(portid, pkts, cnt);
}

/* Setup and start probe_timer */
void probe_start(uint64_t hz, uint8_t lcoreid)
{
	uint64_t interval = 0;
	uint8_t i = 0;

	interval = hz / PROBE_RATE_PER_SEC;

	/* init timer structure */
	rte_timer_init(&probe_timer);

	LOG_DEBUG("start probe timer: interval %lu, lcore %u",
					interval, lcoreid);
	rte_timer_reset(&probe_timer, interval, PERIODICAL, lcoreid,
				   __process_probe, NULL);

	for (i = 0; i < nb_probe; i++) {
		struct probe_ctl *ctl = &probe_list[i];

		if (!port_is_enabled(ctl->portid))
			continue;

		__measure_rd_time_cost(ctl->portid);
	}
}

/* stop probe_timer */
void probe_stop(void)
{
	rte_timer_stop_sync(&probe_timer);
}

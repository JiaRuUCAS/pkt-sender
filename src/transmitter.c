#include "util.h"
#include "pktsender.h"
#include "transmitter.h"
#include "port.h"

#include <rte_common.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_memory.h>
#include <rte_version.h>
#include <rte_cycles.h>

/* default tx rate in unit of bps */
#define TX_RATE_DEFAULT_BPS	102400

/* calculate cycles per byte */
static inline double
__get_tx_cycles_per_byte(uint64_t bps)
{
	return (pktsender.cpu_hz * 8.0 / bps);
}

/* init tx_ctl */
void tx_ctl_init(struct tx_ctl *ctl, struct ether_addr *port_mac,
				uint8_t queueid)
{
	struct pkt_seq *global = &pktsender.tx_pkt;

	/* zero out the entire tx_ctl space */
	memset(ctl, 0, sizeof(struct tx_ctl));

	ctl->queueid = queueid;
	/* set tx_pattern and tx_rate based on global setting */
	ctl->tx_pattern = pktsender.tx_pattern;
	ctl->rate_bps = (pktsender.tx_rate == 0) ?
					TX_RATE_DEFAULT_BPS : (pktsender.tx_rate);
	ctl->rate_cycles = __get_tx_cycles_per_byte(ctl->rate_bps);
	LOG_DEBUG("%s: bps %lu, cycles %lf",
					__FUNCTION__, ctl->rate_bps, ctl->rate_cycles);
	/* init default packet sequence */
	pkt_seq_init_local(&ctl->tx_seq, global, port_mac);
	LOG_DEBUG("Init tx_seq, pkt_len global %u, local %u",
					global->pkt_len, ctl->tx_seq.pkt_len);
}

/** Pre-init all mbuf in the mempool */
static inline void
__pktmbuf_setup_cb(struct rte_mempool *mp,
				void *opaque, void *obj,
				unsigned obj_idx __rte_unused)
{
	struct tx_ctl *tx_ctl = (struct tx_ctl*)opaque;
	struct rte_mbuf *m = (struct rte_mbuf*)obj;

	if (tx_ctl->tx_pattern == TX_PATTERN_SINGLE) {
		struct tx_single *single = &tx_ctl->u.tx_single;

		if (single->is_init == 0) {
			/* construct static packet template */
			pkt_seq_construct_pkt(&tx_ctl->tx_seq, &single->hdr);
			single->is_init = 1;
		}

		/* copy the packet template into the mbuf */
		rte_memcpy((uint8_t*)m->buf_addr + m->data_off,
						(uint8_t*)&single->hdr, MAX_PKT_LEN);
		m->pkt_len = tx_ctl->tx_seq.pkt_len;
		m->data_len = tx_ctl->tx_seq.pkt_len;

//		LOG_DEBUG("Setup cb %u", m->pkt_len);

	} else if (tx_ctl->tx_pattern == TX_PATTERN_RANDOM) {
		// TODO setup random packets
	} else if (tx_ctl->tx_pattern == TX_PATTERN_PCAP) {
		// TODO setup pcap packets
	}
}

/** Setup the default packets to be sent */
void
tx_ctl_setup_mempool(struct tx_ctl *tx_ctl, struct rte_mempool *mp)
{
	if (tx_ctl == NULL || mp == NULL) {
		LOG_ERROR("Wrong parameters: tx_ctl %p, mp %p", tx_ctl, mp);
		return;
	}

	DEBUG_TRACE();

#if RTE_VERSION >= RTE_VERSION_NUM(16, 7, 0, 0)
	rte_mempool_obj_iter(mp, __pktmbuf_setup_cb, tx_ctl);
#else
	{
		struct rte_mbuf *m, *mm;
		uint16_t idx = 0;

		mm = NULL;

		/* allocate each mbuf and put them on a list to be freed. */
		for (;;) {
			if ((m = rte_pktmbuf_alloc(mp)) == NULL)
				break;
			/* put the allocated mbuf into a list to be freed later */
			m->next = mm;
			mm = m;

			__pktmbuf_setup_cb(mp, tx_ctl, m, 0);
//			LOG_DEBUG("setup pkt[%u] %u", idx, m->pkt_len);
			idx++;
		}

		if (mm != NULL)
			rte_pktmbuf_free(mm);
	}
#endif
	LOG_DEBUG("Setup all mbufs in the mempool for queue %u",
					tx_ctl->queueid);
}

static inline void
__pktmbuf_reset(struct rte_mbuf *m)
{
	m->next = NULL;
	m->nb_segs = 1;
	m->port = 0xff;

	m->data_off = (RTE_PKTMBUF_HEADROOM <= m->buf_len) ?
		RTE_PKTMBUF_HEADROOM : m->buf_len;
}

//static void
//__mbuf_dump(struct rte_mbuf *mbuf __rte_unused)
//{
//	struct udpip_hdr *udpip = NULL;
//
//	udpip = rte_pktmbuf_mtod_offset(mbuf,
//					struct udpip_hdr *, sizeof(struct ether_hdr));
//	LOG_DEBUG("UDP length %u, IP length %u",
//					udpip->udp.dgram_len, udpip->ip.total_length);
//}

/**
 * Allocate a bulk of mbufs, initialize refcnt and reset the fields to default
 * values.
 *
 *  @param pool
 *    The mempool from which mbufs are allocated.
 *  @param mbufs
 *    Array of pointers to mbufs
 *  @param count
 *    Array size
 *  @return
 *   - Negative value on failure
 *	 - Number of bytes in total
 */
static inline int
__pktmbuf_alloc_bulk(struct rte_mempool *pool,
		      struct rte_mbuf **mbufs, unsigned count)
{
	unsigned idx = 0;
	int rc;

	rc = rte_mempool_get_bulk(pool, (void * *)mbufs, count);
	if (unlikely(rc))
		return rc;

	/* To understand duff's device on loop unwinding optimization, see
	 * https://en.wikipedia.org/wiki/Duff's_device.
	 * Here while() loop is used rather than do() while{} to avoid extra
	 * check if count is zero.
	 */
	switch (count % 4) {
	case 0:
		while (idx != count) {
#ifdef RTE_ASSERT
			RTE_ASSERT(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#else
			RTE_VERIFY(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#endif
			rte_mbuf_refcnt_set(mbufs[idx], 1);
			__pktmbuf_reset(mbufs[idx]);
			idx++;
			/* fall-through */
		case 3:
#ifdef RTE_ASSERT
			RTE_ASSERT(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#else
			RTE_VERIFY(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#endif
			rte_mbuf_refcnt_set(mbufs[idx], 1);
			__pktmbuf_reset(mbufs[idx]);
			idx++;
			/* fall-through */
		case 2:
#ifdef RTE_ASSERT
			RTE_ASSERT(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#else
			RTE_VERIFY(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#endif
			rte_mbuf_refcnt_set(mbufs[idx], 1);
			__pktmbuf_reset(mbufs[idx]);
			idx++;
			/* fall-through */
		case 1:
#ifdef RTE_ASSERT
			RTE_ASSERT(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#else
			RTE_VERIFY(rte_mbuf_refcnt_read(mbufs[idx]) == 0);
#endif
			rte_mbuf_refcnt_set(mbufs[idx], 1);
			__pktmbuf_reset(mbufs[idx]);
			idx++;
		}
	}
	return 0;
}

/** senda burst of packets as fast as possible */
static inline void
__send_burst(uint8_t portid, uint8_t queueid,
				struct mbuf_table *buffer)
{
	struct rte_mbuf **pkts = buffer->m_table;
	uint32_t ret = 0;
	uint32_t cnt = buffer->len;

	while (cnt > 0) {
		ret = rte_eth_tx_burst(portid, queueid, pkts, cnt);

		pkts += ret;
		cnt -= ret;
//		if (ret > 0)
//			LOG_DEBUG("Port %u: tx %u packets", portid, ret);
	}
}

#define pkt_wire_size(len) (len + FRAME_EXTRA_BYTES)

/** send a set of packet buffers to a given port */
int
tx_ctl_tx_burst(uint8_t portid, struct tx_ctl *ctl,
				struct rte_mempool *mp)
{
	int rc = 0;
	uint64_t cycles = rte_get_tsc_cycles();
	struct rte_mbuf *pkt = NULL;
	uint8_t i = 0;

	if (ctl->rate_next_cycles >= cycles)
		return 0;

	/* Fetch a set of pre-init packets */
//	rc = rte_mempool_get_bulk(mp, (void**)ctl->tx_buffer.m_table,
//						MAX_PKT_BURST);
	rc = __pktmbuf_alloc_bulk(mp, ctl->tx_buffer.m_table, MAX_PKT_BURST);

	if (rc < 0) {
		LOG_ERROR("No enough %u mbufs in the port %u tx_mp",
						MAX_PKT_BURST, portid);
		return -1;
	}

	for (i = 0; i < MAX_PKT_BURST; i++) {
		pkt = ctl->tx_buffer.m_table[i];
		ctl->tx_buffer.total_size += pkt_wire_size(pkt->pkt_len);
	}

	ctl->tx_buffer.len = MAX_PKT_BURST;
//	LOG_DEBUG("Port %u: fetch %u packets, total size %u",
//					portid, MAX_PKT_BURST, ctl->tx_buffer.total_size);

	__send_burst(portid, ctl->queueid, &ctl->tx_buffer);

	/* set next tx cycles */
	ctl->rate_next_cycles = cycles +
				(uint64_t)(ctl->tx_buffer.total_size * ctl->rate_cycles);

	rte_pktmbuf_free_bulk(ctl->tx_buffer.m_table, MAX_PKT_BURST);
	ctl->tx_buffer.len = 0;
	ctl->tx_buffer.total_size = 0;
//		LOG_DEBUG("%u, %u, %lu, %lu, %lu",
//						lcore,
//						ctl->tx_buffer.total_size,
//						ctl->rate_cycles,
//						cycles,
//						ctl->rate_next_cycles);
	return 0;
}

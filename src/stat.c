#include "util.h"
#include "stat.h"
#include "pktsender.h"
#include "port.h"

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_common.h>
#include <rte_timer.h>

/** per-port statistics */
static struct port_stats *port_stat = NULL;

/** statistics timer */
static struct rte_timer stat_timer;

void stat_free(void)
{
	if (port_stat) {
		zfree(port_stat);
	}
	LOG_DEBUG("Free port_stat");
}

/** Initialize per-port statistics */
bool stat_init(uint8_t nb_ports, uint8_t nb_port_enabled)
{
	uint8_t portid = 0, port_cnt = 0;

	if (nb_port_enabled < 1 || nb_ports < 1) {
		LOG_ERROR("Wrong parameter, nb_ports %u, nb_port_enabled %u",
						nb_ports, nb_port_enabled);
		return false;
	}

	port_stat = (struct port_stats*)malloc(
					sizeof(struct port_stats) * nb_port_enabled);
	if (port_stat == NULL) {
		LOG_ERROR("Failed to allocate memory for port_stat");
		return false;
	}

	memset(port_stat, 0, sizeof(struct port_stats) * nb_port_enabled);

	// reset hardware statistics
	for (portid = 0; portid < nb_ports; portid++) {
		if (!port_is_enabled(portid))
			continue;

		rte_eth_stats_reset(portid);
		rte_eth_stats_get(portid, &(port_stat[port_cnt].stat_last));

		port_stat[port_cnt].portid = portid;
		port_stat[port_cnt].cyc_start = rte_get_tsc_cycles();

		port_cnt++;
		if (port_cnt >= nb_port_enabled)
			break;
	}

	LOG_DEBUG("Init port_stat");
	return true;
}

static inline void __calculate_statis(uint8_t portid, struct rte_eth_stats *start,
				struct rte_eth_stats *end)
{
	double tx_bps, rx_bps;
	uint64_t opkts = 0, ipkts = 0;

	opkts = end->opackets - start->opackets;
	ipkts = end->ipackets - start->ipackets;
	tx_bps = ((end->obytes - start->obytes + opkts * FRAME_EXTRA_BYTES) * 8);
	rx_bps = ((end->ibytes - start->ibytes + ipkts * FRAME_EXTRA_BYTES) * 8);

	LOG_INFO("Port %u: TX speed %lf Mbps, %lf kpps",
					portid, (tx_bps / (1024*1024)), opkts / 1000.0);
	LOG_INFO("Port %u: RX speed %lf Mbps, %lf kpps",
				   	portid, (rx_bps / (1024*1024)), ipkts / 1000.0);
}

void stat_stop(void)
{
	uint8_t i, portid;
	struct rte_eth_stats cur_stat;
	uint64_t cycle = 0;

	/* Loop until the timer stopped */
	rte_timer_stop_sync(&stat_timer);

	for (i = 0; i < pktsender.nb_port_enabled; i++) {
		portid = port_stat[i].portid;

		rte_eth_stats_get(portid, &cur_stat);
		cycle = rte_get_tsc_cycles() - port_stat[i].cyc_start;

		LOG_INFO("Port %u: Running %lf seconds.", portid,
					((double)(cycle) / pktsender.cpu_hz));
		LOG_INFO("Port %u: total RX %lu packets (%lu bytes),"
						" TX %lu packets (%lu bytes).",
						portid, cur_stat.ipackets, cur_stat.ibytes,
						cur_stat.opackets, cur_stat.obytes);
	}
}

static void
__process_stat(struct rte_timer *timer __rte_unused,
				void *arg __rte_unused)
{
	uint8_t i = 0;
	struct port_stats *stat = NULL;
	struct rte_eth_stats cur_stat;

	for (i = 0; i < pktsender.nb_port_enabled; i++) {
		stat = &port_stat[i];

		rte_eth_stats_get(stat->portid, &cur_stat);
		__calculate_statis(stat->portid, &stat->stat_last, &cur_stat);

		stat->stat_last = cur_stat;
	}
}

/* Setup and start statistics timer */
void stat_start(uint8_t lcoreid)
{
	/* init timer structure */
	rte_timer_init(&stat_timer);

	/* Load statistics timer:
	 * Every second, on timer lcore, reloaded automatically. */
	LOG_DEBUG("start statistics timer: interval %lu, lcore %u",
					pktsender.cpu_hz, lcoreid);
	rte_timer_reset(&stat_timer, pktsender.cpu_hz,
					PERIODICAL, lcoreid,
					__process_stat, NULL);
}

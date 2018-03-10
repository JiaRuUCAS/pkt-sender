#include "util.h"
#include "pktsender.h"
//#include "stat.h"
//#include "control.h"
#include "port.h"
#include "probe.h"

#include <rte_common.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_timer.h>

#include <signal.h>
#include <syscall.h>

static uint8_t sig_recved = 0;

static inline bool __is_running(uint8_t job_flags)
{
	if ((pktsender.job_state & job_flags) == 0)
		return false;
	return true;
}

static inline bool __is_tx_running(void)
{
	if (pktsender.job_state & (1 << LCORE_JOB_TX))
		return true;
	return false;
}

/**
 * Global signal handler.
 *
 * The first CTRL-C will stop all TX jobs, the second CTRL-C will safely
 * stop the whole process.
 */
void pktsender_sig_handler(int signo)
{
	int tid;

	if (signo == SIGINT) {
		tid = syscall(SYS_gettid);
		if (sig_recved == 0) {
			LOG_INFO("The first signal %d reveiced by thread %d. Stopping TX.",
							signo, tid);
			pktsender.job_state &= (~(1u << LCORE_JOB_TX));
		} else if (sig_recved == 1) {
			LOG_INFO("The second signal %d reveiced by thread %d."
							" Stopping all jobs.",
							signo, tid);
			pktsender.job_state = 0;
		}
		sig_recved++;
	}
}

static void __process_rx(uint8_t portid,
				struct rte_mbuf *pkts[])
{
	uint16_t nb_rx = 0;
//	uint64_t recv_cyc = 0;

	/* RX from hardware */
	nb_rx = rte_eth_rx_burst(portid, RXQ_RX, pkts, MAX_PKT_BURST);
//	nb_rx = rte_eth_rx_burst(portid, RXQ_RX, pkts, 1);

	if (nb_rx == 0)
		return;

	// TODO: extract latency probe packets and handle them.
	probe_receive(portid, pkts, nb_rx);

	/* free all mbufs */
	rte_pktmbuf_free_bulk(pkts, nb_rx);
//	usleep(200000);
//	LOG_DEBUG("lcore %u RX for port %u, job_state %u",
//					lcoreid, portid, pktsender.job_state);
}

static void __launch_measure_lcore(
				uint8_t lcoreid, struct lcore_conf *conf)
{
	uint8_t portid = 0;
	struct lcore_job *job = NULL;
//
	job = &(conf->jobs[LCORE_JOB_TX]);

	while (__is_running(JOB_FLAGS_ALL)) {
		for (portid = 0; portid < job->nb_ports; portid++) {
			/** check timer */
			rte_timer_manage();
		}
	}
}

static void __launch_rxtx_lcore(
				uint8_t lcoreid, struct lcore_conf *conf)
{
	struct lcore_job *jobs = NULL;
	uint8_t portid = 0, is_err = 0;
	uint8_t nb_rx, nb_tx, *port_list = NULL;
	struct rte_mbuf *pkts_recv[MAX_PKT_BURST];

	jobs = conf->jobs;
	nb_rx = jobs[LCORE_JOB_RX].nb_ports;
	nb_tx = jobs[LCORE_JOB_TX].nb_ports;

	LOG_DEBUG("Lcore %u handles %u rx jobs, %u tx jobs",
					lcoreid, nb_rx, nb_tx);

	while (__is_running(conf->job_flags)) {
		// rx
		port_list = jobs[LCORE_JOB_RX].port_list;
		for (portid = 0; portid < nb_rx; portid++) {
			__process_rx(port_list[portid], pkts_recv);
		}

		// tx
		if (__is_tx_running()) {
			port_list = jobs[LCORE_JOB_TX].port_list;
			for (portid = 0; portid < nb_tx; portid++) {
				if (port_transmit(port_list[portid]) < 0) {
					is_err = 1;
					break;
				}
			}
			if (is_err)
				break;
		}
	}
}

/* Launch a single logical core thread */
int pktsender_launch_one_lcore(void *arg __rte_unused)
{
	uint8_t lcoreid = 0, i = 0;
	struct lcore_conf *conf = NULL;
	int tid = 0;

	lcoreid = rte_lcore_id();
	conf = &pktsender.lcore_list[lcoreid];
	tid = syscall(SYS_gettid);

	/* check for idle lcore */
	for (i = 0; i < LCORE_JOB_MAX; i++) {
		if (conf->jobs[i].nb_ports > 0) {
			conf->job_flags |= (1 << i);
			LOG_DEBUG("Lcore %u job %u", lcoreid, i);
		}
	}
	LOG_DEBUG("Lcore %u (thread %d) job_flags %u",
					lcoreid, tid, conf->job_flags);

	if (conf->job_flags == 0) {
		LOG_WARN("Lcore %u has no job to do, exit....", lcoreid);
		return 0;
	}

	/* Lcore pktsender.stat_lcore is used to measurement and statistics. */
	if (lcoreid == pktsender.stat_lcore) {
		LOG_DEBUG("Run measurement/stat loop at lcore %u", lcoreid);
		__launch_measure_lcore(lcoreid, conf);
		LOG_INFO("Measurement/stat loop finished.");
	}
	else {
		LOG_DEBUG("Run RX/TX loop at lcore %u", lcoreid);
		__launch_rxtx_lcore(lcoreid, conf);
		LOG_INFO("Lcore %u finished.", lcoreid);
	}

	return 0;
}

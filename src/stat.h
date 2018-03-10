#ifndef _PKTSENDER_STAT_H_
#define _PKTSENDER_STAT_H_

#include <stdint.h>
#include <rte_ethdev.h>

/** Per-port statistics */
struct port_stats {
	/** DPDK port id */
	uint8_t portid;
	/** start time */
	uint64_t cyc_start;
	/** the last snapshot */
	struct rte_eth_stats stat_last;
};

/**
 * Initialize per-port statistics
 *
 * @param nb_ports
 *	Number of all DPDK ports (used and unused)
 * @param nb_port_enabled
 *	Number of enabled ports
 */
bool stat_init(uint8_t nb_ports, uint8_t nb_port_enabled);

/**
 * Free port statistics
 */
void stat_free(void);

/**
 * Setup statistics timer and start it
 *
 * @param lcoreid
 *	The lcore used to handle port statistics
 */
void stat_start(uint8_t lcoreid);

/**
 * Stop the statistics timer
 */
void stat_stop(void);

/** 
 * Main loop of the statistics thread
 *
 * @param arg
 *	Not used.
 */
void stat_main_loop(void *arg);

#endif /* _PKTSENDER_STAT_H_ */

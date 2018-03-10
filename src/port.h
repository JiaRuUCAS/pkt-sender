#ifndef _PKTSENDER_PORT_H_
#define _PKTSENDER_PORT_H_

/**
 * @file
 * Port Configuration
 */

#include "pktsender.h"
#include "transmitter.h"

/** Max number of ports */
#define MAX_PORT_NUM	16

/** RX queue assignment of each port */
enum {
	RXQ_RX = 0,
	/** Default number of RX queues per port */
	RXQ_NUM_PER_PORT,
};

/** TX queue assignment of each port */
enum {
	/** txq 0 is used to transmit user-specified major traffic */
	TXQ_TX = 0,
	/** txq 1 is used to transmit latency probe traffic */
	TXQ_LATENCY = 1,
	/** Default number of TX queues per port */
	TXQ_NUM_PER_PORT,
};

/** Default number of RX ring descriptors */
#define RX_DESC_DEFAULT 128
/** Default number of TX ring descriptors */
#define TX_DESC_DEFAULT 512

/** Default number of items in each mbuf mempool */
#define NB_MBUFS	TX_DESC_DEFAULT

/**
 * Port Information Structure
 */
struct port_info {
	/** DPDK port id */
	uint8_t id;
	/** whether in use */
	uint8_t is_enabled;
	/** MAC address */
	struct ether_addr mac;
	/** RX mbuf mempool */
	struct rte_mempool *rx_mp;
	/** TX mbuf mempool */
	struct rte_mempool *tx_mp;
	/** lcore mappings of each job */
	uint8_t lcore_map[LCORE_JOB_MAX];
	/** TX controller  */
	struct tx_ctl tx_ctl;
};

/**
 * Check whether the port is enabled.
 *
 * @param portid
 * @return
 *	- True if port is enabled.
 *	- False if port is not enabled.
 */
bool port_is_enabled(uint8_t portid);

/**
 * Update the lcore mapping of a port job
 *
 * @param portid
 */
void port_update_lcore(uint8_t portid, uint8_t lcoreid, uint8_t job);

/**
 * Parse a portmask and initialize port_list array
 *
 * @param portmask
 * @return
 *	The number of ports enabled
 */
uint8_t port_parse_opt(unsigned long portmask);

/**
 * Initialize all ports
 *
 * @return
 *	- 0 on success
 *  - Negative value on failure
 *	- - ERR_OUT_OF_RANGE: the queue id exceeds the max value of queues
 *	- - ERR_DPDK: error from dpdk calls
 */
int port_init(void);

/**
 * Start all ports
 *
 * @return
 *	- 0 on success
 *  - Negative value on failure
 *	- - ERR_DPDK: error from dpdk calls
 */
int port_start(void);

/**
 * dump all enabled ports
 */
void port_dump(void);

/**
 * Close all enabled ports
 */
void port_close(void);

/**
 * Free port_list and all related memory areas
 */
void port_free(void);

/**
 * Transmit on a given port
 *
 * @param portid
 *	DPDK port id
 */
int port_transmit(uint8_t portid);

/**
 * Get port-local pkt_seq structure
 *
 * @param portid
 * @return
 *	- NULL on failure
 *	- Pointer to the pkt_seq structure on success
 */
struct pkt_seq *port_get_pkt_seq(uint8_t portid);

#endif /* _PKTSENDER_PORT_H_ */

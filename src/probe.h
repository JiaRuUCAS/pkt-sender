#ifndef _PKTSENDER_PROBE_H_
#define _PKTSENDER_PROBE_H_

#include "pkt_seq.h"

/** Payload of probe packets */
struct probe_payload {
	/** Probe ID */
	uint64_t probe_idx;
	/** Sender port ID */
	uint32_t probe_sender;
	/** Magic value */
	uint32_t probe_magic;
	/** padding to 18 bytes */
	uint8_t pad[2];
} __attribute__((__packed__));

/** Probe packets (64-bit UDP packet) */
struct probe_pkt {
	/** Ethernet header */
	struct ether_hdr eth;
	/** IPv4 + UDP headers */
	struct udpip_hdr udpip;
	/** Payload of probe packets */
	struct probe_payload data;
} __attribute__((__packed__));

#define PROBE_PKT_ETHER_TYPE	0x88F7
/** Length of probe packets */
#define PROBE_PKT_LEN	60
/** L3 protocol of probe packets */
#define PROBE_PKT_PROTO	IPPROTO_UDP
/** Magic value of probe packets */
#define PROBE_PKT_MAGIC 0x12345678
/** Number of probe packets sent per second */
#define PROBE_RATE_PER_SEC	10
/** Max number of probe IDs */
#define PROBE_PKT_MAX	256
/** Mempool cache size */
#define PROBE_MP_CACHE	100

/** Probe controller */
struct probe_ctl {
	/** DPDK port ID */
	uint8_t portid;
	/** DPDK queue ID used for probe */
	uint8_t queueid;
//	/** TX output file */
//	FILE *tx_output;
//	/** RX output file */
//	FILE *rx_output;
	/** Next probe idx */
	uint64_t next_idx;
	/** Next probe packet to send */
	struct rte_mbuf *next_pkt;
	/** Packet configuration */
	struct pkt_seq pkt_configure;
};

/**
 * Initialize the global probe_ctl list
 *
 * @param nb_ports
 *	Number of all ports in DPDK
 * @return
 *	- 0 on success
 *	- Negative value on failure
 */
int probe_init(uint8_t nb_ports);

/**
 * Free all memory areas related to probe
 */
void probe_free(void);

/**
 * Setup and start probe_timer
 *
 * @param hz
 *	The cpu frequence of this system
 * @param lcoreid
 *	The lcore to run the probe timer
 */
void probe_start(uint64_t hz, uint8_t lcoreid);

/**
 * Stop probe_timer
 */
void probe_stop(void);

/**
 * Receive probe packets
 *
 * @param portid
 *	The port where the packets received from
 * @param pkts
 *	Array of packets
 * @param cnt
 *	Number of valid packets in the array pkts
 */
void probe_receive(uint8_t portid, struct rte_mbuf *pkts[], uint16_t cnt);

#endif /* _PKTSENDER_PROBE_H_ */

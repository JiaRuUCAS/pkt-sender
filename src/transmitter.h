#ifndef _PKTSENDER_TRANSMITTER_H_
#define _PKTSENDER_TRANSMITTER_H_

#include "pktsender.h"

/** Controller of single-pkt-pattern transmittion */
struct tx_single {
	/** Whether the following part is initialized */
	uint8_t is_init;
	/** Packet header */
	struct pkt_hdr hdr;
	/** padding for full mbuf */
	uint8_t pad[MAX_PKT_LEN - sizeof(struct pkt_hdr)];
};

/** Controller of pcap-pattern transmittion */
struct tx_pcap {
	/** Pcap file */
	FILE *file;
};

/** TX buffer */
struct mbuf_table {
	/** Total size of all packets in the buffer */
	uint32_t total_size;
	/** Number of mbufs in the buffer */
	uint16_t len;
	/** Array of pointers to the mbufs */
	struct rte_mbuf *m_table[MAX_PKT_BURST];
};

/** Controller of transmittion */
struct tx_ctl {
	/** TX queue id */
	uint8_t queueid;
	/** tx pattern */
	uint8_t tx_pattern;
	/** default packet sequence */
	struct pkt_seq tx_seq;
	/** controller of specific pattern */
	union {
		struct tx_single tx_single;
		struct tx_pcap tx_pcap;
	} u;
	/** TX buffer */
	struct mbuf_table tx_buffer;
	/** Rate control: TX rate in unit of bps */
	uint64_t rate_bps;
	/** Rate control: cycles per byte */
	double rate_cycles;
	/** Rate control: next cycle to send a burst of traffic */
	uint64_t rate_next_cycles;
};

/**
 * Initialize a tx_ctl structure
 *
 * @param ctl
 *	Pointer to the tx_ctl structure need to initialize
 * @param port_mac
 *	The MAC address of the port who has this tx_ctl
 */
void tx_ctl_init(struct tx_ctl *ctl, struct ether_addr *port_mac,
				uint8_t queueid);

/**
 * Setup the default packets to be sent in the mempool
 *
 * @param tx_ctl
 *	Pointer to the tx_ctl structure
 * @param mp
 *	Pointer to the mempool need to be setup
 */
void tx_ctl_setup_mempool(struct tx_ctl *tx_ctl, struct rte_mempool *mp);

/**
 * Send a set of packet buffers to a given port
 *
 * @param portid
 *	The port to send to
 * @param ctl
 *	Pointer to the tx_ctl structure
 * @param mp
 *	Pointer to the tx mempool of this port
 */
int tx_ctl_tx_burst(uint8_t portid, struct tx_ctl *ctl,
				struct rte_mempool *mp);

#endif /* _PKTSENDER_TRANSMITTER_H_ */

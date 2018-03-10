#ifndef _PKT_SEQ_H_
#define _PKT_SEQ_H_

/**
 * @file
 * Packet sequence
 */

#include <rte_ether.h>
#include <rte_eth_ctrl.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>

/** IPv4 + TCP headers */
struct tcpip_hdr {
	/** IPv4 header */
	struct ipv4_hdr ip;
	/** tcp header */
	struct tcp_hdr tcp;
} __attribute__((__packed__));

/** IPv4 + UDP headers */
struct udpip_hdr {
	/** IPv4 header */
	struct ipv4_hdr ip;
	/** udp header */
	struct udp_hdr udp;
} __attribute__((__packed__));

/** Packet header */
struct pkt_hdr {
	/** Ethernet header */
	struct ether_hdr eth;
	/** union of all headers */
	union {
		/** IPv4 */
		struct ipv4_hdr ip;
		/** TCP */
		struct tcpip_hdr tcpip;
		/** UDP */
		struct udpip_hdr udpip;
	} u;
} __attribute__((__packed__));

/* Default packet sequence configuration */
/** Default destination mac address */
#define PKT_SEQ_MAC_DST	"21:21:21:21:21:21"
/** Default source ipv4 address */
#define PKT_SEQ_IP_SRC	IPv4(192,168,0,12)
/** Default destination ipv4 address */
#define PKT_SEQ_IP_DST	IPv4(192,168,0,21)
/** Default length of packet */
#define PKT_SEQ_PKT_LEN 60
/** Default L3 protocol */
#define PKT_SEQ_PROTO IPPROTO_UDP
/** Default source port */
#define PKT_SEQ_PORT_SRC 9312
/** Default destination port */
#define PKT_SEQ_PORT_DST 9321

/** Default tcp sequence number */
#define PKT_SEQ_TCP_SEQ 0x12345678
/** Default tcp ack number */
#define PKT_SEQ_TCP_ACK 0x12345690
/** Default tcp flags */
#define PKT_SEQ_TCP_FLAGS TCP_ACK_FLAG
/** Default tcp window size */
#define PKT_SEQ_TCP_WINDOW 8192

/** Structure of a packet sequence */
struct pkt_seq {
	/** source mac address */
	struct ether_addr src_mac;
	/** destination mac address */
	struct ether_addr dst_mac;
	/** source ipv4 address */
	uint32_t src_ip;
	/** destination ipv4 address */
	uint32_t dst_ip;
	/** L3 protocol */
	uint8_t proto;
	/** source port */
	uint16_t src_port;
	/** destination port */
	uint16_t dst_port;
	/** length of packet (excluding FCS)*/
	uint16_t pkt_len;
};

/**
 * Convert a MAC address string to a ether_addr structure.
 *
 * @param str
 *	A MAC address string, formatted as "a:b:c:d:e:f"
 * @param addr
 *	Pointer to the ether_addr structure to store the result
 * @return
 *	- 0 on success
 *	- Negative value on failure
 */
int pkt_seq_parse_mac(const char *str, struct ether_addr *addr);

/**
 * Initialize per-port local pkt_seq
 *
 * @param local
 *	Pointer to the local pkt_seq
 * @param global
 *	Pointer to the global pkt_seq. If NULL, use default
 *	values to initialize the local one.
 * @param port_mac
 *	The mac address of the port. It will be used to initialize
 *	the source mac address of the local pkt_seq.
 */
void pkt_seq_init_local(struct pkt_seq *local, struct pkt_seq *global,
				struct ether_addr *port_mac);

/**
 * Construct packet based on the pkt_seq value
 *
 * @param pkt
 *	Pointer to the pkt_seq structure.
 * @param hdr
 *	Pointer to the start of memory area that is used to store the packet.
 */
void pkt_seq_construct_pkt(struct pkt_seq *pkt, void *hdr);

#endif /* _PKT_SEQ_H_ */

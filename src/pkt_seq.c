#include "util.h"
#include "pkt_seq.h"

#include <rte_hash_crc.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>

#define IP_VERSION 0x40
#define IP_HDRLEN 0x05
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)
#define IP_TTL_DEF 64

/* convert a MAC address string to ether_addr structure */
int pkt_seq_parse_mac(const char *str, struct ether_addr *addr)
{
	unsigned int mac[6] = {0};
	int ret = 0, i = 0;

	ret = sscanf(str, "%x:%x:%x:%x:%x:%x",
			&mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
	if (ret < 6) {
		LOG_ERROR("Failed to parse MAC from string %s", str);
		return ERR_FORMAT;
	}

	for (i = 0; i < 6; i++)
		addr->addr_bytes[i] = (uint8_t)(mac[i] & 0xFF);

	return 0;
}

///* convert a host 64bit number to MAC address in network byte order */
//static inline void
//inet_h64tom(uint64_t value, struct ether_addr *eaddr)
//{
//	eaddr->addr_bytes[5] = ((value >> 0) & 0xFF);
//	eaddr->addr_bytes[4] = ((value >> 8) & 0xFF);
//	eaddr->addr_bytes[3] = ((value >> 16) & 0xFF);
//	eaddr->addr_bytes[2] = ((value >> 24) & 0xFF);
//	eaddr->addr_bytes[1] = ((value >> 32) & 0xFF);
//	eaddr->addr_bytes[0] = ((value >> 40) & 0xFF);
//}

/** initialize per-port local pkt_seq */
void
pkt_seq_init_local(struct pkt_seq *local,
				struct pkt_seq *global, struct ether_addr *port_mac)
{
	if (global == NULL) {
		pkt_seq_parse_mac(PKT_SEQ_MAC_DST, &local->dst_mac);

		local->src_ip = PKT_SEQ_IP_SRC;
		local->dst_ip = PKT_SEQ_IP_DST;
		local->proto = PKT_SEQ_PROTO;
		local->src_port = PKT_SEQ_PORT_SRC;
		local->dst_port = PKT_SEQ_PORT_DST;
		local->pkt_len = PKT_SEQ_PKT_LEN;
	} else {
		ether_addr_copy(&global->dst_mac, &local->dst_mac);

		local->src_ip = global->src_ip;
		local->dst_ip = global->dst_ip;
		local->proto = global->proto;
		local->src_port = global->src_port;
		local->dst_port = global->dst_port;
		local->pkt_len = global->pkt_len;
	}

	if (port_mac != NULL)
		ether_addr_copy(port_mac, &local->src_mac);
}

/* calculate 16-bit CRC checksum */
static inline uint16_t
__checksum_16(const void *data, uint32_t len)
{
	uint32_t crc32 = 0;

	crc32 = rte_hash_crc(data, len, 0);

	crc32 = (crc32 & 0xffff) + (crc32 >> 16);
	crc32 = (crc32 & 0xffff) + (crc32 >> 16);

	return ~((uint16_t)crc32);
}

/**
 * Construct ethernet header
 *
 * @return
 *	Pointer to memory after the ethernet header.
 */
static char *
__construct_eth_hdr(struct pkt_seq *pkt, void *hdr)
{
	struct ether_hdr *eth = (struct ether_hdr*)hdr;

	ether_addr_copy(&pkt->src_mac, &eth->s_addr);
	ether_addr_copy(&pkt->dst_mac, &eth->d_addr);
	eth->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	return (char *)(eth + 1);
}

/** Construct ipv4 header */
static void
__construct_ipv4_hdr(struct pkt_seq *pkt, void *hdr)
{
	struct ipv4_hdr *ip = (struct ipv4_hdr*)hdr;

	/* zero out the header space */
	memset((char *)ip, 0, sizeof(struct ipv4_hdr));

	/* Construct IPv4 header */
	ip->version_ihl = IP_VHL_DEF;
	ip->type_of_service = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = IP_TTL_DEF;
	ip->packet_id = 0;
	ip->src_addr = rte_cpu_to_be_32(pkt->src_ip);
	ip->dst_addr = rte_cpu_to_be_32(pkt->dst_ip);
	ip->next_proto_id = pkt->proto;
	/* the entire packet size in bytes, including header and data */
	ip->total_length = rte_cpu_to_be_16(
					pkt->pkt_len - sizeof(struct ether_hdr));

	/* Compute IPv4 header checksum */
	ip->hdr_checksum = __checksum_16(ip, sizeof(struct ipv4_hdr));
}

/**
 * Construct TCP header
 */
static void
__construct_tcp_hdr(struct pkt_seq *pkt, void *hdr)
{
	uint16_t tlen = 0;
	struct tcpip_hdr *tcpip = (struct tcpip_hdr*)hdr;

	tlen = pkt->pkt_len - sizeof(struct ether_hdr)
						- sizeof(struct ipv4_hdr);

	/* zero out the header space */
	memset(tcpip, 0, sizeof(struct tcpip_hdr));

	/* Construct TCP header */
	tcpip->tcp.src_port = rte_cpu_to_be_16(pkt->src_port);
	tcpip->tcp.dst_port = rte_cpu_to_be_16(pkt->dst_port);
	tcpip->tcp.sent_seq = rte_cpu_to_be_32(PKT_SEQ_TCP_SEQ);
	tcpip->tcp.recv_ack = rte_cpu_to_be_32(PKT_SEQ_TCP_ACK);
	tcpip->tcp.data_off = ((sizeof(struct tcp_hdr) / sizeof(uint32_t)) << 4);
	tcpip->tcp.tcp_flags = PKT_SEQ_TCP_FLAGS;
	tcpip->tcp.rx_win = rte_cpu_to_be_16(PKT_SEQ_TCP_WINDOW);
	tcpip->tcp.tcp_urp = 0;

	/* Construct part of IP header */
	tcpip->ip.src_addr = rte_cpu_to_be_32(pkt->src_ip);
	tcpip->ip.dst_addr = rte_cpu_to_be_32(pkt->dst_ip);
	tcpip->ip.total_length = rte_cpu_to_be_16(tlen);
	tcpip->ip.next_proto_id = IPPROTO_TCP;

	/* Calculate tcp checksum */
	tcpip->tcp.cksum = __checksum_16(tcpip, tlen);
}

/**
 * Construct UDP header
 */
static void
__construct_udp_hdr(struct pkt_seq *pkt, void *hdr)
{
	struct udpip_hdr *udpip = (struct udpip_hdr *)hdr;
	uint16_t tlen = 0, crc = 0;

	/* zero out the header space */
	memset(udpip, 0, sizeof(struct udpip_hdr));

	tlen = pkt->pkt_len - sizeof(struct ether_hdr)
						- sizeof(struct ipv4_hdr);

	/* Construct pseudo IPv4 header */
	udpip->ip.next_proto_id = IPPROTO_UDP;
	/* In udp checksum compution, the total_length of IP pseudo header
	 * equals to the length of udp datagram (udp header + udp payload).
	 */
	udpip->ip.total_length = rte_cpu_to_be_16(tlen);
	udpip->ip.src_addr = rte_cpu_to_be_32(pkt->src_ip);
	udpip->ip.dst_addr = rte_cpu_to_be_32(pkt->dst_ip);

	/* Construct UDP header */
	udpip->udp.src_port = rte_cpu_to_be_16(pkt->src_port);
	udpip->udp.dst_port = rte_cpu_to_be_16(pkt->dst_port);
	udpip->udp.dgram_len = rte_cpu_to_be_16(tlen);

	/* Calculate UDP checksum */
	crc = __checksum_16(udpip, tlen);
	if (crc == 0)
		crc = 0xFFFF;
	udpip->udp.dgram_cksum = crc;
}

/** Construct packet based on the pkt_seq value */
void
pkt_seq_construct_pkt(struct pkt_seq *pkt, void *hdr)
{
	char *l3_hdr = NULL;

	/* construct ethernet header */
	l3_hdr = __construct_eth_hdr(pkt, hdr);

	if (pkt->proto == IPPROTO_UDP) {
		/* construct UDP header */
		__construct_udp_hdr(pkt, l3_hdr);
	} else if (pkt->proto == IPPROTO_TCP) {
		/* construct TCP header */
		__construct_tcp_hdr(pkt, l3_hdr);
	}

	/* construct IPv4 header */
	__construct_ipv4_hdr(pkt, l3_hdr);
}

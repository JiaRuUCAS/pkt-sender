#ifndef _PKTSENDER_H_
#define _PKTSENDER_H_

#include <rte_memory.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>

#include "pkt_seq.h"

/** Burst size */
#define MAX_PKT_BURST	32

/** Cache size of mbuf mempool */
#define MEMPOOL_CACHE_SIZE	256

/** Max number of cpu sockets */
#define NB_SOCKETS 4

/** Configure how many packets ahead to preferch, when reading packets */
#define PREFETCH_OFFSET 3

/** Max number of ports per lcore job */
#define MAX_PORT_PER_JOB	4

/** Length of the interpacket gap field in Ethernet frame */
#define INTER_FRAME_GAP	12
/** Length of the preamble and start frame delimiter field */
#define PKT_PREAMBLE_SIZE 8
/** Length of the frame check sequence (FCS) field */
#define FCS_SIZE 4

/** Max length of one packet */
#define MAX_PKT_LEN	(ETHER_MAX_LEN - FCS_SIZE)

/** Total length of extra ethernet fields */
#define FRAME_EXTRA_BYTES (INTER_FRAME_GAP + \
				PKT_PREAMBLE_SIZE + FCS_SIZE)

/** lcore job type */
enum {
	LCORE_JOB_RX = 0,
	LCORE_JOB_TX,
//	LCORE_JOB_LATENCY,
	LCORE_JOB_MAX,
};

/** Job flags if all jobs are running */
#define JOB_FLAGS_ALL 0x3

/** port list of a job */
struct lcore_job {
	/** Number of ports */
	uint8_t nb_ports;
	/** Array of port ids */
	uint8_t port_list[MAX_PORT_PER_JOB];
};

/** Per-lcore configuration */
struct lcore_conf {
	/** Job bits: 1 if this lcore has the corresponding job */
	uint8_t job_flags;
	/** Array of lcore jobs */
	struct lcore_job jobs[LCORE_JOB_MAX];
} __rte_cache_aligned;

struct port_info;

/** Global data of pkt-sender */
struct pktsender {
	/** Number of all ports in DPDK (used and unused) */
	uint8_t nb_ports;
	/** Number of ports enabled */
	uint8_t nb_port_enabled;
	/** Pointer to the array of port informations */
	struct port_info *port_list;
	/** Number of lcores in use */
	uint8_t nb_lcores;
	/** Lcore configurations */
	struct lcore_conf lcore_list[RTE_MAX_LCORE];
	/** The lcore used to statistics */
	uint8_t stat_lcore;
	/** CPU HZ of this system */
	uint64_t cpu_hz;
	/** Global job state */
	uint8_t job_state;
	/** TX pattern */
	uint8_t tx_pattern;
	/** Global TX packet configuration */
	struct pkt_seq tx_pkt;
	/** TX rate in unit of bps */
	uint64_t tx_rate;
};

/** Transmition pattern */
enum {
	/** single packet */
	TX_PATTERN_SINGLE = 0,
	/** random packets */
	TX_PATTERN_RANDOM,
	/** packets from pcap */
	TX_PATTERN_PCAP,
};

/** Global pkt-sender configuration data */
extern struct pktsender pktsender;

/**
 * Launch a single logical core thread.
 *
 * @param arg
 *	Not used.
 */
int pktsender_launch_one_lcore(void *arg);

/**
 * Free a list of packet mbufs back into its original mempool.
 *
 * Free a list of mbufs by calling rte_pktmbuf_free() in a loop as a
 * wrapper function.
 *
 * @param m_list
 *   An array of rte_mbuf pointers to be freed.
 * @param npkts
 *   Number of packets to free in m_list.
 */
static inline void __attribute__((always_inline))
rte_pktmbuf_free_bulk(struct rte_mbuf *m_list[], int16_t npkts)
{
	while (npkts--)
		rte_pktmbuf_free(*m_list++);
}

/**
 * Global signal handler.
 *
 * The first CTRL-C will stop all TX jobs, the second CTRL-C will safely
 * stop the whole process.
 *
 * @param signo
 *	The signal received. Note that only SIGINT is handled.
 */
void pktsender_sig_handler(int signo);

#endif /* _PKTSENDER_H_ */

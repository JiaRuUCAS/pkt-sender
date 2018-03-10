#ifndef _PKTSENDER_TRACER_H_
#define _PKTSENDER_TRACER_H_

#include <stdint.h>
#include <sys/time.h>

/** Length of probe packet*/
#define PROBE_PKT_LEN		60
/** Frame type of probe packet: PTP */
#define PROBE_ETHER_TYPE	0x88F7
/** Magic value of probe packets */
#define PROBE_MAGIC	0x12345678
/** PTP messge ID */
#define PROBE_PTP_MSG		0x00
/** PTP version number */
#define PROBE_PTP_VERSION	0x02


/** Pre-defined location ID */
enum {
	/** Hardware TX */
	LOC_HARDWARE_TX = 0,
	/** Hardwarw RX */
	LOC_HARDWARE_RX,
	/** Software TX */
	LOC_SOFTWARE_TX,
	/** Software RX */
	LOC_SOFTWARE_RX,
};

/** Format of probe packet: PTP */
struct pkt_fmt {
	/** Source mac */
	uint8_t src_mac[6];
	/** Destination mac */
	uint8_t dst_mac[6];
	/** Frame type: MUST be FMT_ETHER_TYPE */
	uint16_t ether_type;
	/** PTP message ID: MUST be 0x00 */
	uint8_t ptp_msg_id;
	/** PTP version: MUST be 0x02 */
	uint8_t ptp_version;
	/** Probe ID */
	uint64_t probe_idx;
	/** Sender port ID */
	uint32_t probe_sender;
	/** Magic value: MUST be FMT_PROBE_MAGIC */
	uint32_t probe_magic;
} __attribute__((__packed__));

/** Type of timestamp */
enum {
	/** CPU cycles */
	TIMESTAMP_CYCLES = 0,
	/** struct timespec read from NIC */
	TIMESTAMP_TIMESPEC,
};

/** Timestamp structure */
struct record_ts {
	/** Timestamp type */
	uint8_t ts_type;
	/** Timestamp */
	union {
		/** Timespec read from NIC */
		struct timespec timespec;
		/** Cycles read from CPU */
		uint64_t cycles;
	} u;
};

/** Format of trace record */
struct record_fmt {
	/** thread ID */
	int tid;
	/** location */
	uint8_t location;
	/** Sender ID */
	uint32_t probe_sender;
	/** Probe pkt ID */
	uint64_t probe_idx;
	/** Timestamp */
	struct record_ts timestamp;
};

struct rte_mbuf;

/**
 * Flush cache: dump all records in the cache to file
 */
void trace_flush(void);

/**
 * Identify probe packets and record trace.
 *
 * @param port
 *	The port where these packets were received from
 * @param pkts
 *	Array of packets
 * @param cnt
 *	Number of packets in the array pkts
 * @param loc
 *	The location where the packets are at.
 */
void trace_handler(uint8_t port, struct rte_mbuf *pkts[], int cnt, uint8_t loc);

/**
 * Prepare for recording TX timestamp directly from hardware
 *
 * @param portid
 *	port to send the packet
 * @param buf
 *	array of mbufs of pkt to be sent
 * @param cnt
 *	Number of mbuf in the array
 * @return
 *	- 0 if there are at least one probe packet
 *	- -1 : No probe packet
 */
int trace_hw_tx_prepare(uint8_t portid, struct rte_mbuf *buf[], int cnt);

/**
 * Read hardware TX timestamp and record it.
 *
 * @param portid
 *	port to send the packet
 * @param pkt
 *	Probe packet need to record. If NULL, use the data in the local_info
 */
void trace_hw_tx_record(uint8_t portid, struct rte_mbuf *pkt);

/**
 * Record the receiving of a probe packet
 *
 * Use IEEE1588 PTP packets as probe packets. When hardware receives a
 * PTP packet, the hardware will record the RX time in the specific
 * registers.
 *
 * @param portid
 *	Port where the packets are received from.
 * @param pkts
 *	Array of packets received
 * @param cnt
 *	Number of packets in the array
 */
void trace_hw_rx_record(uint8_t portid, struct rte_mbuf *pkts[], int cnt);

///**
// * Record a trace directly
// */
//void trace_record(uint8_t loc, uint32_t sender,
//				uint64_t idx, uint64_t cycles);

#endif /* _PKTSENDER_TRACER_H_ */

#include "pt_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include "../src/util.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>

#include <unistd.h>
#include <sys/syscall.h>

#define PER_THREAD_CACHE_SIZE	10

#define OUTPUT_PREFIX "trace_"

struct local_info {
	int tid;
	FILE *fout;
	uint32_t nb_record;
	struct record_fmt cache[PER_THREAD_CACHE_SIZE];
	uint64_t last_idx;
	uint32_t last_port;
};

static __thread struct local_info local_info = {
	.tid = 0,
	.fout = NULL,
	.nb_record = 0,
	.cache = {
		{
			.tid = 0,
			.location = 0,
			.probe_sender = 0,
			.probe_idx = 0,
		}
	},
	.last_idx = 0,
	.last_port = 0,
};

static int __local_init(void)
{
	char buf[16] = {0};

	/* get thread id */
	local_info.tid = syscall(SYS_gettid);

	/* Open trace file.
	 * Note: there is no calling for fclose. When a process exits, all files
	 * opened by it will be closed automatically by Linux kernel.
	 */
	snprintf(buf, 16, "%s%d", OUTPUT_PREFIX, local_info.tid);
	local_info.fout = fopen(buf, "w");
	if (local_info.fout == NULL) {
		fprintf(stderr, "[TRACER ERROR]: Failed to open trace file %s", buf);
		local_info.tid = -1;
		return -1;
	}

	fprintf(stderr, "init thread %d, file %s\n", local_info.tid, buf);
	return 0;
}

static inline void
writen(FILE *fp, void *buf, size_t len)
{
	size_t ret = len, n = 0;

	while (ret > 0) {
		n = fwrite((char*)buf, sizeof(char), len, fp);
		buf += n;
		ret -= n;
	}
}

/* dump all records in the cache into file */
void
trace_flush(void)
{
	if (local_info.nb_record == 0)
		return;

	writen(local_info.fout, local_info.cache,
					sizeof(struct record_fmt) * local_info.nb_record);
	local_info.nb_record = 0;
}

static void
__record_to_cache(uint8_t loc, uint64_t idx, uint32_t sender,
				uint8_t type, struct timespec *ts)
{
	struct record_fmt *record =
				&local_info.cache[local_info.nb_record];

	if (local_info.fout == NULL) {
		if (__local_init() < 0)
			return;
	}

	if (ts == NULL)
		return;

	record->tid = local_info.tid;
	record->location = loc;
	record->probe_sender = sender;
	record->probe_idx = idx;
	record->timestamp.ts_type = type;

	if (type == TIMESTAMP_CYCLES)
		record->timestamp.u.cycles = ts->tv_nsec;
	else if (type == TIMESTAMP_TIMESPEC)
		record->timestamp.u.timespec = *ts;

//	LOG_DEBUG("RECORD loc %u, port %u, idx %lu, time sec %lu nsec %lu",
//					loc, sender, idx, ts->tv_sec, ts->tv_nsec);

	/* if cache is full, dump to file */
	local_info.nb_record++;
	if (local_info.nb_record >= PER_THREAD_CACHE_SIZE) {
		writen(local_info.fout, local_info.cache,
						sizeof(struct record_fmt) * PER_THREAD_CACHE_SIZE);
		local_info.nb_record = 0;
	}
}

void trace_handler(uint8_t port, struct rte_mbuf *pkts[], int cnt, uint8_t loc)
{
	struct pkt_fmt *fmt = NULL;
	int i = 0, is_read = 0;
	struct timespec ts = {0,0};

	if (local_info.tid < 0)
		return;

	if (local_info.fout == NULL) {
		if (__local_init() < 0)
			return;
	}

	for (i = 0; i < cnt; i++) {
		fmt = rte_pktmbuf_mtod(pkts[i], struct pkt_fmt *);

		if (fmt->ether_type != rte_cpu_to_be_16(PROBE_ETHER_TYPE))
			continue;
//		if (!(pkts[i]->ol_flags & PKT_RX_IEEE1588_TMST))
//			continue;

		/* get timestamp */
		if (!is_read) {
#ifdef PT_NIC_TIMESTAMP
			rte_eth_timesync_read_time(port, &ts);
#else
			ts.tv_nsec = rte_rdtsc();
#endif
			is_read = 1;
		}

#ifdef PT_NIC_TIMESTAMP
		__record_to_cache(loc, fmt->probe_idx, fmt->probe_sender,
						TIMESTAMP_TIMESPEC, &ts);
#else
		__record_to_cache(loc, fmt->probe_idx, fmt->probe_sender,
						TIMESTAMP_CYCLES, &ts);
#endif
	}
}

/** prepare for recording TX from hardware */
int trace_hw_tx_prepare(uint8_t portid, struct rte_mbuf *buf[], int cnt)
{
//	struct ether_hdr *eth = NULL;
	struct timespec ts;
	int i = 0, ret = -1;
	struct rte_mbuf *pkt = NULL;
	struct pkt_fmt *fmt = NULL;
	uint16_t ptp_be = rte_cpu_to_be_16(PROBE_ETHER_TYPE);

	if (local_info.tid < 0)
		return -1;

	for (i = 0; i < cnt; i++) {
		pkt = buf[i];
		fmt = rte_pktmbuf_mtod(pkt, struct pkt_fmt *);

		if (fmt->ether_type != ptp_be)
			continue;

		/* Enable flag for hardware timestamping. */
		if ((pkt->ol_flags & PKT_TX_IEEE1588_TMST) == 0) {
			pkt->ol_flags |= PKT_TX_IEEE1588_TMST;
			ret = 0;
		}

//		ts.tv_sec = 0;
//		ts.tv_nsec = 0;
//		rte_eth_timesync_read_time(portid, &ts);
//		LOG_DEBUG("Port %u time %lu %lu", portid, ts.tv_sec, ts.tv_nsec);
//		__record_to_cache(LOC_SOFTWARE_TX, fmt->probe_idx,
//						fmt->probe_sender, &ts);
	}

	if (ret == 0) {
		/* Read value from NIC to prevent latching with old value. */
		rte_eth_timesync_read_tx_timestamp(portid, &ts);
		return ret;
	} else {
		/* No probe packet */
		return -1;
	}
}

/* read hardware TX timestamp and record it. */
void
trace_hw_tx_record(uint8_t portid, struct rte_mbuf *pkt)
{
	struct pkt_fmt *fmt = NULL;
	int wait_us = 0, ret = 0;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = 0,
	};

	if (local_info.tid < 0)
		return;

	/* Wait at least 1 us to read TX timestamp. */
	while (((ret = rte_eth_timesync_read_tx_timestamp(portid, &ts)) < 0)
					&& (wait_us < 1000)) {
		rte_delay_us(1);
		wait_us++;
	}

	if (wait_us == 1000) {
		LOG_ERROR("Failed to read HW TX timestamp, ret %d", ret);
		return;
	}

	if (pkt == NULL) {
		__record_to_cache(LOC_HARDWARE_TX, local_info.last_idx,
						local_info.last_port, TIMESTAMP_TIMESPEC, &ts);	
	} else {
		fmt = rte_pktmbuf_mtod(pkt, struct pkt_fmt *);
		__record_to_cache(LOC_HARDWARE_TX, fmt->probe_idx,
						fmt->probe_sender, TIMESTAMP_TIMESPEC, &ts);
	}
	trace_flush();
}

/* record hardware RX */
void
trace_hw_rx_record(uint8_t portid, struct rte_mbuf *pkts[], int cnt)
{
	struct rte_mbuf *pkt = NULL;
	struct pkt_fmt *fmt = NULL;
	int i = 0, ret = 0;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = 0,
	};

	if (local_info.tid < 0)
		return;

	for (i = 0; i < cnt; i++) {
		pkt = pkts[i];

		if ((pkt->ol_flags & PKT_RX_IEEE1588_TMST) == 0)
			continue;
//		else {
//			LOG_DEBUG("Recv packet with timestamp");
//		}

		fmt = rte_pktmbuf_mtod(pkt, struct pkt_fmt *);
//		if (fmt->ether_type != rte_cpu_to_be_16(PTP_PROTOCOL))
//			continue;

//		/* record software RX timestamp */
//		ts.tv_sec = 0;
//		ts.tv_nsec = 0;
//		rte_eth_timesync_read_time(portid, &ts);
//		__record_to_cache(LOC_SOFTWARE_RX, fmt->probe_idx,
//						fmt->probe_sender, &ts);

//		LOG_DEBUG("Recv probe packet %lu", fmt->probe_idx);

		/* record hardware RX timestamp */
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		ret = rte_eth_timesync_read_rx_timestamp(portid, &ts, 0);
		if (ret < 0) {
			LOG_ERROR("Failed to read HW RX timestamp, ret %d", ret);
		} else {
			__record_to_cache(LOC_HARDWARE_RX, fmt->probe_idx,
							fmt->probe_sender, TIMESTAMP_TIMESPEC, &ts);
		}

		/* flush cache to output file */
		trace_flush();
	}
}

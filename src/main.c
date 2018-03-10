/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdarg.h>
#include <getopt.h>
#include <signal.h>

#include "util.h"
#include "port.h"
#include "pktsender.h"
#include "stat.h"
#include "probe.h"
//#include "pkt_seq.h"

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_string_fns.h>
#include <rte_cycles.h>
#include <rte_timer.h>

/** Global data */
struct pktsender pktsender = {
	.nb_ports = 0,
	.nb_port_enabled = 0,
	.port_list = NULL,
	.nb_lcores = 0,
	.stat_lcore = 0,
	.cpu_hz = 0,
	.job_state = 0,
	.tx_pattern = TX_PATTERN_SINGLE,
	.tx_pkt = {
		.src_ip = PKT_SEQ_IP_SRC,
		.dst_ip = PKT_SEQ_IP_DST,
		.proto = PKT_SEQ_PROTO,
		.src_port = PKT_SEQ_PORT_SRC,
		.dst_port = PKT_SEQ_PORT_DST,
		.pkt_len = PKT_SEQ_PKT_LEN,
	},
	.tx_rate = 0,
};

#define MAX_LCORE_PARAMS 128

struct lcore_params {
	uint8_t port_id;
	uint8_t job;
	uint8_t lcore_id;
} __rte_cache_aligned;

static struct lcore_params lcore_params_array[MAX_LCORE_PARAMS];

static struct lcore_params *lcore_params;
static uint16_t nb_lcore_params;

static unsigned long portmask = 0;

/* prefix of output file */
static char *prefix = NULL;

#define OPTION_CONFIG	"config"
#define OPTION_MAC_DST	"mac-dst"

/**
 * Initialize lcore_conf and port info
 */
static int32_t __init_lcore_port(void)
{
	uint16_t i;
	uint8_t lcore, port, job;
	struct lcore_job *lcore_job = NULL;

	for (i = 0; i < nb_lcore_params; ++i) {
		lcore = lcore_params[i].lcore_id;
		port = lcore_params[i].port_id;
		job = lcore_params[i].job;

		/* update lcore_conf */
		lcore_job = &pktsender.lcore_list[lcore].jobs[job];
		if (lcore_job->nb_ports >= MAX_PORT_PER_JOB) {
			LOG_ERROR("Number of ports handled by lcore %u job %u "
					  "exceeds the max value %u",
					  lcore, job, MAX_PORT_PER_JOB);
			return ERR_OUT_OF_RANGE;
		}
		lcore_job->port_list[lcore_job->nb_ports] = port;
		lcore_job->nb_ports++;

		/* update the lcore mapping of the port */
		if (lcore != pktsender.stat_lcore)
			port_update_lcore(port, lcore, job);
	}
	return 0;
}

/* display usage */
static void __print_usage(const char *prgname)
{
	printf("%s [EAL options] -- -p <PORTMASK> -r <tx_rate> -o <output_prefix>"
		" -- "OPTION_MAC_DST" <destination MAC>"
		"  --"OPTION_CONFIG" (port,R/T,lcore)[,(port,R/T,lcore]\n"
		"  -p <PORTMASK>: mask of enabled ports\n"
		"  -r <tx_rate>: per-port transmit rate (bps), s.t. \"1G\", \"20M\"\n"
		"  -o <output_prefix>: prefix of output file name\n"
		"  --"OPTION_MAC_DST": destination mac address of packets sent\n"
		"  --"OPTION_CONFIG": port-lcore mapping configuration\n",
		prgname);
}

static int32_t __parse_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	enum fieldnames {
		FLD_PORT = 0,
		FLD_JOB,
		FLD_LCORE,
		_NUM_FLD
	};
	char *str_fld[_NUM_FLD];
	uint32_t size;
	uint8_t val = 0;

	nb_lcore_params = 0;

	while ((p = strchr(p0, '(')) != NULL) {
		if (nb_lcore_params >= MAX_LCORE_PARAMS) {
			printf("exceeded max number of lcore params: %hu\n",
				nb_lcore_params);
			return -1;
		}

		++p;
		p0 = strchr(p, ')');
		if (p0 == NULL)
			return -1;

		size = p0 - p;
		if (size >= sizeof(s))
			return -1;

		snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') !=
				_NUM_FLD)
			return -1;

		// parse port id
		if (!str_to_uint8(str_fld[FLD_PORT], &val))
			return -1;
		lcore_params_array[nb_lcore_params].port_id = val;

		// parse lcore id
		if (!str_to_uint8(str_fld[FLD_LCORE], &val))
			return -1;
		lcore_params_array[nb_lcore_params].lcore_id = val;

		// parse RX/TX/Latency
		if (strcmp(str_fld[FLD_JOB], "R") == 0)
			lcore_params_array[nb_lcore_params].job = LCORE_JOB_RX;
		else if (strcmp(str_fld[FLD_JOB], "T") == 0)
			lcore_params_array[nb_lcore_params].job = LCORE_JOB_TX;
		else
			return -1;

		++nb_lcore_params;
	}
	lcore_params = lcore_params_array;
	return 0;
}

#define __STRNCMP(name, opt) (!strncmp(name, opt, sizeof(opt)))
static int32_t __parse_args_long_options(
				struct option *lgopts, int32_t option_index)
{
	int32_t ret = -1;
	const char *optname = lgopts[option_index].name;

	if (__STRNCMP(optname, OPTION_CONFIG)) {
		ret = __parse_config(optarg);
		if (ret < 0)
			LOG_ERROR("invalid config");
	} else if (__STRNCMP(optname, OPTION_MAC_DST)) {
		ret = pkt_seq_parse_mac(optarg, &pktsender.tx_pkt.dst_mac);
	}

	return ret;
}
#undef __STRNCMP

/**
 * Check port/job/lcore mapping
 *
 * A valid mapping should satisfy:
 * 	- Both port and lcore are enabled.
 * 	- This queue is not mapped to another lcore.
 *  - No duplicate mapping.
 * @return
 *	- The number of valid mappings.
 */
static int __check_mapping(void)
{
	uint16_t i = 0, j = 0, real_cnt = 0;
	struct lcore_params *lcore_p = NULL, *iter = NULL;
	bool is_dup = false;
	uint8_t nb_port_max = 0;

	LOG_INFO("Checking port/(R/T)job/lcore mappings......");

	nb_port_max = pktsender.nb_ports;

	for (i = 0; i < nb_lcore_params; i++) {
		lcore_p = &lcore_params[i];

		/* check whether the port_id is valid */
		if (lcore_p->port_id >= nb_port_max) {
			LOG_WARN("Invalid port id %u, valid range is [0,%u)",
							lcore_p->port_id, nb_port_max);
			continue;
		}

		/* check whether the port is enabled */
		if (!port_is_enabled(lcore_p->port_id)) {
			LOG_WARN("Port %u is not enabled.", lcore_p->port_id);
			continue;
		}

		/* check whether the lcore is enabled */
		if (!rte_lcore_is_enabled(lcore_p->lcore_id)) {
			LOG_WARN("Lcore %u is not enabled.", lcore_p->lcore_id);
			continue;
		}

		/* check whether the lcore is statistics lcore */
		if (lcore_p->lcore_id == pktsender.stat_lcore) {
			LOG_WARN("Lcore %u is used as statistics lcore, no RX/TX can "
					 "be assigned to it.", lcore_p->lcore_id);
			continue;
		}

		/* check for duplication */
		is_dup = false;
		for (j = 0; j < real_cnt; j++) {
			iter = &lcore_params[j];
			if (lcore_p->port_id == iter->port_id &&
							lcore_p->job == iter->job) {
				is_dup = true;
				if (lcore_p->lcore_id == iter->lcore_id) {
					LOG_WARN("Found duplicate port/job/lcore mapping for (%u,%u,%u),"
									" remove the duplicate item",
									iter->port_id, iter->job, iter->lcore_id);
				} else {
					LOG_WARN("Queue (%u,%u) is assigned to two lcore (%u and %u)"
									" remove the mapping of lcore %u.",
									iter->port_id, iter->job, iter->lcore_id,
									lcore_p->lcore_id, lcore_p->lcore_id);
				}
				break;
			}
		}
		if (is_dup)
			continue;

		/* If needed, update the lcore_params array. */
		if (real_cnt < i) {
			lcore_params[real_cnt].port_id = lcore_p->port_id;
			lcore_params[real_cnt].job = lcore_p->job;
			lcore_params[real_cnt].lcore_id = lcore_p->lcore_id;
		}

		/* increase real_cnt */
		real_cnt++;
	}

	LOG_INFO("Checking finished. %u invalid mappings are removed.",
					(nb_lcore_params - real_cnt));

	// add measurement thread jobs
	for (i = 0; i < nb_port_max; i++) {
		if (!port_is_enabled(i))
			continue;

		lcore_params[real_cnt].port_id = i;
		lcore_params[real_cnt].job = LCORE_JOB_TX;
		lcore_params[real_cnt].lcore_id = pktsender.stat_lcore;
		real_cnt++;

		if (real_cnt >= MAX_LCORE_PARAMS)
			break;
	}

#ifdef PS_DEBUG
	for (i = 0; i < real_cnt; i++) {
		LOG_DEBUG("MAP %u, port %u, job %u, lcore %u", i,
						lcore_params[i].port_id,
						lcore_params[i].job,
						lcore_params[i].lcore_id);
	}
#endif

	return real_cnt;
}

static uint8_t __parse_portmask(const char *str)
{
	char *end = NULL;

	portmask = strtoul(str, &end, 16);
	if ((str[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;
	if ((portmask == 0) && errno)
		return -1;

	return 0;
}

/* Format: e.g 1000k => 1000 kbps, 2m => 2 mbps,
 * 			   128 => 128 bps
 */
static uint64_t
__parse_rate(const char *rate_str)
{
	long val = 0;
	char *unit = NULL;
	uint64_t tx_rate = 0;

	val = strtol(rate_str, &unit, 10);
	if (errno == EINVAL || errno == ERANGE
					|| unit == rate_str) {
		LOG_ERROR("Failed to parse TX rate %s", rate_str);
		return 0;
	}

	if (val < 0) {
		LOG_ERROR("Wrong rate value %ld", val);
		return 0;
	}

	switch(*unit) {
		case 'k':	case 'K':
			tx_rate = val << 10;
			break;
		case 'm':	case 'M':
			tx_rate = val << 20;
			break;
		case 'g':	case 'G':
			tx_rate = val << 40;
			break;
		default:
			tx_rate = val;
	}

	LOG_INFO("Set tx rate to %lu bps (%lf Mbps)",
					tx_rate, (tx_rate / (double)(1024 * 1024)));
	return tx_rate;
}

static int32_t
__parse_args(int32_t argc, char **argv)
{
	int32_t opt, ret;
	char **argvopt;
	int32_t option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{OPTION_CONFIG, 1, 0, 0},
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:r:o",
				lgopts, &option_index)) != EOF) {
		switch (opt) {
		case 'p':
			ret = __parse_portmask(optarg);
			if (ret < 0) {
				LOG_ERROR("Wrong port mask %s", optarg);
				__print_usage(prgname);
				return -1;
			}
			break;
		case 'r':
			pktsender.tx_rate = __parse_rate(optarg);
			break;
		case 'o':
			prefix = strdup(optarg);
			break;
		case 0:
			if (__parse_args_long_options(lgopts, option_index)) {
				__print_usage(prgname);
				return -1;
			}
			break;
		default:
			__print_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 1; /* reset getopt lib */

	return ret;
}

static void __pktsender_free(void)
{
	/* free port_list */
	port_free();
	/* free port_stat */
	stat_free();
	/* free probe_list */
	probe_free();

	if (prefix)
		free(prefix);
}

int32_t main(int32_t argc, char **argv)
{
	int32_t ret;
	uint32_t lcoreid;
//	uint8_t socket_id, port_id;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		LOG_ERROR("Failed to initialize EAL, ret %d.", ret);
		return -1;
	}
	argc -= ret;
	argv += ret;

	pktsender.nb_ports = rte_eth_dev_count();
	pktsender.nb_lcores = rte_lcore_count();
	pktsender.cpu_hz = rte_get_tsc_hz();
	pkt_seq_init_local(&pktsender.tx_pkt, NULL, NULL);

	/* parse application arguments (after the EAL ones) */
	ret = __parse_args(argc, argv);
	if (ret < 0) {
		LOG_ERROR("Invalid parameters");
		return -1;
	}

	pktsender.nb_port_enabled = port_parse_opt(portmask);
	if (pktsender.nb_port_enabled < 1) {
		LOG_ERROR("At least 1 port is needed.");
		return -1;
	}

	/* Set stat_lcore */
	pktsender.stat_lcore = rte_get_master_lcore();
	LOG_DEBUG("Statistics lcore is %u", pktsender.stat_lcore);

	/* Check port/job/lcore mappings */
	nb_lcore_params = __check_mapping();
	if (nb_lcore_params == 0) {
		LOG_ERROR("No valid port/job/lcore found.");
		goto fail_free_all;
	}

	signal(SIGINT, pktsender_sig_handler);

	/* init DPDK timer library */
	rte_timer_subsystem_init();

	ret = __init_lcore_port();
	if (ret < 0) {
		LOG_ERROR("__init_lcore_port failed");
		goto fail_free_all;
	}

	if (port_init() < 0) {
		goto fail_free_all;
	}

	port_dump();

	/* init port statistics */
	if (!stat_init(pktsender.nb_ports, pktsender.nb_port_enabled)) {
		LOG_ERROR("Failed to initialize per-port statistics");
		goto fail_free_all;
	}

	/* init probe */
	if (probe_init(pktsender.nb_ports) < 0) {
		LOG_ERROR("Failed to initialize global probe structure");
		goto fail_free_all;
	}

	/* start ports */
	ret = port_start();
	if (ret < 0) {
		LOG_ERROR("Failed to start ports");
		goto fail_free_all;
	}

	/* start statistics timer */
	stat_start(pktsender.stat_lcore);

	/* start probe timer */
	probe_start(pktsender.cpu_hz, pktsender.stat_lcore);

	/* set job state to all running */
	pktsender.job_state = JOB_FLAGS_ALL;

	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(pktsender_launch_one_lcore, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcoreid) {
		if (rte_eal_wait_lcore(lcoreid) < 0)
			return -1;
	}

	LOG_INFO("Closing ports......");
	port_close();
	LOG_INFO("Done.");

	/* stop probe timer */
	probe_stop();

	/* stop statistics */
	stat_stop();

	__pktsender_free();
	return 0;

fail_free_all:
	__pktsender_free();
	return -1;
}

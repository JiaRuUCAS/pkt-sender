#include "util.h"
#include "cmd_dump.h"
#include "pt_trace.h"
#include "ringbuffer.h"
#include "cuckoohash.h"

#include "getopt.h"

/** default output file name */
#define OUTPUT_DEFAULT	"trace.data"

/** Max number of locations */
#define CMD_DUMP_LOC_MAX	16

/** Max number of trace points */
#define CMD_DUMP_TRACE_MAX	(16384)

static FILE *fout = NULL;
static const char *output_str;
static int nb_input = 0;

/** CPU frequency in unit of GHz */
static double cpu_hz = 0;

/** trace point id */
struct trace_id {
	uint32_t portid;
	uint64_t probeid;
};

static int mac_loc = 0;

/** trace data */
struct trace_data {
	int idx;
	struct record_ts timestamp[CMD_DUMP_LOC_MAX];
	int tids[CMD_DUMP_LOC_MAX];
};

static struct cuckoohash_tbl *trace_tbl = NULL;

static struct trace_data trace_data[CMD_DUMP_TRACE_MAX];

static struct ringbuffer *trace_data_ring = NULL;

/* init trace_tbl */
static int
__init_trace_tbl(void)
{
	int ret = 0, i = 0;

	ret = cuckoohash_create(&trace_tbl, sizeof(struct trace_id),
					CMD_DUMP_TRACE_MAX);
	if (ret < 0) {
		LOG_ERROR("Failed to create trace table");
		return ret;
	}

	trace_data_ring = ringbuffer_create(NULL,
							sizeof(int) * CMD_DUMP_TRACE_MAX);
	if (trace_data_ring == NULL) {
		LOG_ERROR("Failed to create ringbuffer for trace free idx");
		cuckoohash_destroy(trace_tbl);
		trace_tbl = NULL;
		return ERR_MEMORY;
	}

	/* populate the free slots ring */
	for (i = 0; i < CMD_DUMP_TRACE_MAX; i++) {
		ringbuffer_put(trace_data_ring, (char *)&i, sizeof(int));
	}
	return 0;
}

/* free trace_tbl */
static void
__free_trace_tbl(void)
{
	if (trace_tbl) {
		cuckoohash_destroy(trace_tbl);
		trace_tbl = NULL;
	}

	if (trace_data_ring) {
		ringbuffer_destroy(trace_data_ring);
		trace_data_ring = NULL;
	}
}

static int
__add_record(struct record_fmt *record)
{
	struct trace_id key;
	struct trace_data *tracedata;
	void *data;
	int ret = 0, idx = 0;

	key.portid = record->probe_sender;
	key.probeid = record->probe_idx;

	// find existing trace
	ret = cuckoohash_lookup_data(trace_tbl, &key, (void**)&data);

	// If there is no match, add new trace into table
	if (ret < 0) {
		// get free idx
		ret = ringbuffer_get(trace_data_ring, (char*)&idx, sizeof(int));
		if (ret != sizeof(int)) {
			LOG_ERROR("No enough space for new trace (%u,%lu)",
							key.portid, key.probeid);
			return ERR_OUT_OF_RANGE;
		}

		// construct new trace data
		tracedata = &trace_data[idx];
		memset(tracedata, 0, sizeof(struct trace_data));
		tracedata->idx = idx;
		tracedata->timestamp[record->location] = record->timestamp;
		tracedata->tids[record->location] = record->tid;

		// put into trace_tbl
		ret = cuckoohash_add_key_data(trace_tbl, &key, (void*)tracedata);
		if (ret < 0) {
			LOG_ERROR("Failed to add new trace into trace_tbl");
			return ret;
		}
	}
	// If match, update trace data
	else {
		tracedata = (struct trace_data *)data;
		tracedata->timestamp[record->location] = record->timestamp;
		tracedata->tids[record->location] = record->tid;
	}

	return 0;
}

/* Convert timestamp in the trace to nanosecond */
static inline uint64_t
__get_time_ns(struct record_ts *timestamp)
{
	if (timestamp->ts_type == TIMESTAMP_CYCLES) {
		return (uint64_t)(timestamp->u.cycles * cpu_hz);
	} else if (timestamp->ts_type == TIMESTAMP_TIMESPEC) {
		return (timestamp->u.timespec.tv_sec * 1000000000
						+ timestamp->u.timespec.tv_nsec);
	}
	return 0;
}

/* dump all traces */
static int
__dump_all_traces(FILE *fp)
{
	uint32_t iter = 0;
	const void *key;
	void *data;
	const struct trace_id *traceid = NULL;
	struct trace_data *tracedata = NULL;
	int i = 0, nb_trace = 0;
	uint64_t time = 0;

	// print title
	fprintf(fp, "portid\tprobeid");
	for (i = 0; i <= mac_loc; i++) {
		fprintf(fp, "\tloc%d_tid\tloc%d_nsec", i, i);
	}
	fprintf(fp, "\n");

	// print trace
	while (cuckoohash_iterate(trace_tbl, &key,
							(void**)&data, &iter) >= 0) {
		if (data == NULL)
			continue;

		tracedata = (struct trace_data*)data;

		traceid = (struct trace_id*)key;
		fprintf(fp, "%u\t%lu", traceid->portid, traceid->probeid);

		for (i = 0; i <= mac_loc; i++) {
			time = __get_time_ns(&tracedata->timestamp[i]);
			fprintf(fp, "\t%d\t%lu", tracedata->tids[i], time);
		}
		fprintf(fp, "\n");
		nb_trace++;
	}
	return nb_trace;
}

void cmd_dump_usage(void)
{
	fprintf(stdout, "Usage: pt_analyzer [-o <file>] <tracefile1>"
					" ... [tracefile%d]\n", CMD_DUMP_FILE_MAX);
	fprintf(stdout, "    -o <file>: Set output file path and name\n");
}

static int32_t
__parse_args(int argc, char **argv)
{
	int opt;
	char **argvopt;
//	int opt_idx;
//	char *progname = argv[0];

	argvopt = argv;

	while ((opt = getopt(argc, argvopt, "o:")) != -1) {
		switch (opt) {
			case 'o':
				fout = fopen(optarg, "w");
				if (fout == NULL) {
					LOG_ERROR("Failed to open output file %s", optarg);
					return -1;
				}
				output_str = optarg;
				LOG_DEBUG("open output file %s", optarg);
				break;
			default:
				LOG_ERROR("Unknown option -%c", opt);
				cmd_dump_usage();
				return -1;
		}
	}
	return optind;
}

static inline void
__free_all(void)
{
	if (fout) {
		fclose(fout);
		fout = NULL;
	}
	__free_trace_tbl();
}

static void
__read_record(FILE *file)
{
	struct record_fmt record;

	if (file == NULL)
		return;

	while ((fread(&record, sizeof(struct record_fmt),
									1, file)) > 0) {
//		LOG_DEBUG("%d\t%u\t%u\t%lu\t%lu",
//						record.tid, record.location,
//						record.probe_sender,
//						record.probe_idx,
//						record.cycles);

		if (record.location > mac_loc)
			mac_loc = record.location;

		if (__add_record(&record) < 0) {
			LOG_WARN("Failed to add record");
		}

		if (feof(file)) {
			LOG_DEBUG("End of file");
			break;
		}
	}
}

int cmd_dump(int argc, char **argv)
{
	int ret = 0, i;
	FILE *fin[CMD_DUMP_FILE_MAX] = {NULL};

	ret = __parse_args(argc, argv);
	if (ret < 0) {
		__free_all();
		return -1;
	}

	argc -= ret;
	argv += ret;

	// If no user-specific output file, use the default value
	if (fout == NULL) {
		fout = fopen(OUTPUT_DEFAULT, "w");
		if (fout == NULL) {
			LOG_ERROR("Failed to open default output file %s",
							OUTPUT_DEFAULT);
			return -1;
		}
		output_str = OUTPUT_DEFAULT;
	}

	// Open all input files
	nb_input = argc;
	for (i = 0; i < nb_input; i++) {
		fin[i] = fopen(argv[i], "r");
		if (fin[i] == NULL) {
			LOG_ERROR("Failed to trace file %s", argv[i]);
			goto free_input;
		}
	}

	// create trace table
	ret = __init_trace_tbl();
	if (ret < 0) {
		LOG_ERROR("Failed to initialize trace_tbl");
		goto free_trace_tbl;
	}

	// dump traces to output file
	for (i = 0; i < nb_input; i++) {
		if (fin[i] == NULL)
			continue;
		LOG_DEBUG("Load file %s", argv[i]);
		__read_record(fin[i]);
		fclose(fin[i]);
		fin[i] = NULL;
	}

	ret = __dump_all_traces(fout);

	LOG_INFO("Dump %d traces into file %s", ret, output_str);
	__free_all();
	return 0;

free_trace_tbl:
	__free_trace_tbl();

free_input:
	for (i = 0; i < nb_input; i++) {
		if (fin[i]) {
			fclose(fin[i]);
			fin[i] = NULL;
		}
	}
	return -1;
}

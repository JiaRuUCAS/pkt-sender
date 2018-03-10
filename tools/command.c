#include "util.h"
#include "command.h"
#include "cmd_dump.h"

/** Command id */
enum {
	/** "dump" command */
	COMMAND_DUMP = 0,
	/** Max number of commands */
	COMMAND_MAX,
};

/** Command list */
static struct command command_list[COMMAND_MAX] = {
	[COMMAND_DUMP] = {"dump", "Dump binary trace file(s) to a specific"
							  " file in a human-readable table format.",
						CMD_DUMP_ARG_MIN,
						cmd_dump_usage, cmd_dump},
};

struct command *cmd_lookup(const char *cmd)
{
	int i = 0;
	struct command *iter = NULL;

	for (i = 0; i < COMMAND_MAX; i++) {
		iter = &command_list[i];
		if (strcmp(iter->name, cmd) == 0) {
			return iter;
		}
	}
	return NULL;
}

void cmd_usage(void)
{
	int i = 0;

	fprintf(stdout, "Usage: pt_analyzer <command> [arguments]\n");
	for (i = 0; i < COMMAND_MAX; i++) {
		fprintf(stdout, "    pt_analyzer %s:\n", command_list[i].name);
		fprintf(stdout, "        %s\n", command_list[i].description);
	}
}

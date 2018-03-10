#include "util.h"
#include "command.h"


int main(int argc, char **argv)
{
	struct command *cmd = NULL;
	int ret = 0;

	if (argc < 2) {
		LOG_ERROR("Too few arguments (%d)", argc);
		cmd_usage();
		return 1;
	}

	cmd = cmd_lookup(argv[1]);
	if (cmd == NULL) {
		LOG_ERROR("Unknown command %s", argv[1]);
		cmd_usage();
		return 1;
	}

	argc--;
	argv++;;

	if (cmd->min_argc > (argc - 1)) {
		LOG_ERROR("Too few arguments (%d) for command %s",
						cmd->min_argc, cmd->name);
		cmd->usage();
		return 1;
	}

	ret = cmd->handler(argc, argv);
	return ret;
}

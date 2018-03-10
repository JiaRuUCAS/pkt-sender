#ifndef _PKTSENDER_COMMAND_H_
#define _PKTSENDER_COMMAND_H_

/** Command definition */
struct command {
	/** Command name */
	const char *name;
	/** Description of command */
	const char *description;
	/** Min number of arguments following name */
	int min_argc;
	/** Print command usage */
	void (*usage)(void);
	/** Command handler */
	int (*handler)(int argc, char **argv);
};

/**
 * Find the command from command_list
 *
 * @param cmd
 *	The name of command to lookup
 * @return
 *	- NULL if failed
 */
struct command *cmd_lookup(const char *cmd);

/** Print global usage */
void cmd_usage(void);

#endif /* _PKTSENDER_COMMAND_H_ */

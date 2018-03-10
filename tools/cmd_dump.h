#ifndef _PKTSENDER_CMD_DUMP_H_
#define _PKTSENDER_CMD_DUMP_H_

/** Max number of input trace files */
#define CMD_DUMP_FILE_MAX	10

/** Min number of arguments */
#define CMD_DUMP_ARG_MIN	1

/** Print usage of "dump" command */
void cmd_dump_usage(void);

/** Main processing of "dump" command */
int cmd_dump(int argc, char **argv);

#endif /* _PKTSENDER_CMD_DUMP_H_ */

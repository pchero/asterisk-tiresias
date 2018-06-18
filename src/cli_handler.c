/*
 * cli_handler.c
 *
 *  Created on: Jun 16, 2018
 *      Author: pchero
 */

#include <asterisk.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>
#include <asterisk/module.h>

#include <stdio.h>
#include <jansson.h>

#include "cli_handler.h"
#include "fp_handler.h"

static char* tiresias_show_contexts(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char* tiresias_remove_context(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static char* tiresias_show_audios(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char* tiresias_remove_audio(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);


struct ast_cli_entry cli_tiresias[] = {
		AST_CLI_DEFINE(tiresias_show_contexts, "List all registered tiresias contexts"),
		AST_CLI_DEFINE(tiresias_remove_context, "Remove tiresias context info"),
		AST_CLI_DEFINE(tiresias_show_audios, "Show tiresias context detail info"),
		AST_CLI_DEFINE(tiresias_remove_audio, "Remove tiresias audio info"),
};

bool cli_init(void)
{
	int err;

	err = 0;
	err |= ast_cli_register_multiple(cli_tiresias, ARRAY_LEN(cli_tiresias));

	if(err != 0) {
		cli_term();
		return false;
	}

	return true;
}

bool cli_term(void)
{
	ast_cli_unregister_multiple(cli_tiresias, ARRAY_LEN(cli_tiresias));

	return true;
}

static char* tiresias_show_contexts(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int idx;
	json_t* j_tmps;
	json_t* j_tmp;

	if(cmd == CLI_INIT) {
		e->command = "tiresias show contexts";
		e->usage =
			"Usage: tiresias show contexts\n"
			"	   Lists all registered contexts.\n";
		return NULL;
	}
	else if(cmd == CLI_GENERATE) {
		return NULL;
	}

	j_tmps = fp_get_context_lists_all();
	if(j_tmps == NULL) {
		ast_log(LOG_WARNING, "Could not get context list info.\n");
		return NULL;
	}

	ast_cli(a->fd, "%-36.36s %-70.70s\n", "Name", "Directory");

	json_array_foreach(j_tmps, idx, j_tmp) {
		if(j_tmp == NULL) {
			continue;
		}

		ast_cli(a->fd, "%-36.36s %-70.70s\n",
				json_string_value(json_object_get(j_tmp, "name")) ? : "",
				json_string_value(json_object_get(j_tmp, "directory")) ? : ""
				);

	}
	json_decref(j_tmps);

	return CLI_SUCCESS;
}

/**
 * Shows the given context's audios info
 * @param e
 * @param cmd
 * @param a
 * @return
 */
static char* tiresias_show_audios(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int idx;
	json_t* j_tmp;
	json_t* j_tmps;

	if(cmd == CLI_INIT) {
		e->command = "tiresias show audios";
		e->usage =
			"Usage: tiresias show audios <context name>\n"
			"	   List the registered audio info of given context.\n";
		return NULL;
	}
	else if(cmd == CLI_GENERATE) {
		return NULL;
	}

	if(a->argc != 4) {
		ast_log(LOG_NOTICE, "Wrong input parameter.\n");
		return CLI_SHOWUSAGE;
	}

	j_tmps = fp_get_audio_lists_by_contextname(a->argv[3]);
	if(j_tmps == NULL) {
		ast_cli(a->fd, "Could not find context info. context[%s]\n", a->argv[3]);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "%-36.36s %-45.45s %-36.36s %-36.36s\n", "Uuid", "Name", "Context", "Hash");

	json_array_foreach(j_tmps, idx, j_tmp) {
		ast_cli(a->fd, "%-36.36s %-45.45s %-36.36s %-36.36s\n",
				json_string_value(json_object_get(j_tmp, "uuid")) ? : "",
				json_string_value(json_object_get(j_tmp, "name")) ? : "",
				json_string_value(json_object_get(j_tmp, "context")) ? : "",
				json_string_value(json_object_get(j_tmp, "hash")) ? : ""
				);
	}
	json_decref(j_tmps);

	return CLI_SUCCESS;
}

/**
 * Shows the given context's audios info
 * @param e
 * @param cmd
 * @param a
 * @return
 */
static char* tiresias_remove_audio(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int ret;

	if(cmd == CLI_INIT) {
		e->command = "tiresias remove audio";
		e->usage =
			"Usage: tiresias remove audio <audio uuid>\n"
			"	   Remove the given audio info.\n";
		return NULL;
	}
	else if(cmd == CLI_GENERATE) {
		return NULL;
	}

	if(a->argc != 4) {
		ast_log(LOG_NOTICE, "Wrong input parameter.\n");
		return CLI_SHOWUSAGE;
	}

	ret = fp_delete_audio_list_info(a->argv[3]);
	if(ret == false) {
		ast_cli(a->fd, "Could not remove the audio info. uuid[%s]\n", a->argv[3]);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Removed the audio info. uuid[%s]\n", a->argv[3]);

	return CLI_SUCCESS;
}

/**
 * Remove the given context info
 * @param e
 * @param cmd
 * @param a
 * @return
 */
static char* tiresias_remove_context(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int ret;

	if(cmd == CLI_INIT) {
		e->command = "tiresias remove context";
		e->usage =
			"Usage: tiresias remove context <context name>\n"
			"	   Remove the given context info.\n";
		return NULL;
	}
	else if(cmd == CLI_GENERATE) {
		return NULL;
	}

	if(a->argc != 4) {
		ast_log(LOG_NOTICE, "Wrong input parameter.\n");
		return CLI_SHOWUSAGE;
	}

	ret = fp_delete_context_list_info(a->argv[3]);
	if(ret == false) {
		ast_cli(a->fd, "Could not remove the context info. context[%s]\n", a->argv[3]);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Removed the context info. context[%s]\n", a->argv[3]);

	return CLI_SUCCESS;
}

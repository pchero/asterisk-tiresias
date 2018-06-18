/*
 * application_handler.c
 *
 *  Created on: Jun 16, 2018
 *      Author: pchero
 */


#include <asterisk.h>
#include <asterisk/logger.h>
#include <asterisk/app.h>
#include <asterisk/strings.h>
#include <asterisk/module.h>
#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>


#include <stdio.h>
#include <stdbool.h>
#include <jansson.h>

#include "app_tiresias.h"
#include "fp_handler.h"
#include "application_handler.h"


/*** DOCUMENTATION
	<application name="Tiresias" language="en_US">
		<synopsis>
			Do the tiresias audio fingerprinting
		</synopsis>
		<syntax>
			<parameter name="context" required="true">
				<para>context name.</para>
			</parameter>
			<parameter name="duration">
				<para>fingerprint duration</para>
			</parameter>
		</syntax>
		<description>
			<para>Fingerprint and audio recognise with the given seconds.</para>
		</description>
	</application>
 ***/

#define sfree(p) { if(p != NULL) ast_free(p); p=NULL; }

#define DEF_APPLICATION_TIRESIAS "Tiresias"

#define DEF_DURATION   3000


static int tiresias_exec(struct ast_channel *chan, const char *data);
static int record_voice(struct ast_filestream* file, struct ast_channel *chan, int duration);

static int tiresias_exec(struct ast_channel *chan, const char *data)
{
	int ret;
	char* data_copy;
	char* filename;
	char* tmp;
	const char* tmp_const;
	const char* context;
	int duration;
	struct ast_filestream* file;
	double tolerance;
	json_t* j_fp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(context);
		AST_APP_ARG(duraion);
		AST_APP_ARG(tolerance);
	);

	if (ast_strlen_zero(data) == 1) {
		ast_log(LOG_WARNING, "TIRESIAS requires an argument.\n");
		return -1;
	}

	/* parse the args */
	data_copy = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, data_copy);

	ret = ast_strlen_zero(args.context);
	if(ret == 1) {
		ast_log(LOG_NOTICE, "Wrong context info.\n");
		return -1;
	}
	context = args.context;

	duration = DEF_DURATION;
	ret = ast_strlen_zero(args.duraion);
	if(ret != 1) {
		duration = atoi(args.duraion);
	}

	tolerance = -1;
	tmp_const = json_string_value(json_object_get(json_object_get(g_app->j_conf, "global"), "tolerance"));
	if(tmp_const != NULL) {
		tolerance = atof(tmp_const);
	}
	ret = ast_strlen_zero(args.tolerance);
	if(ret != 1) {
		tolerance = atof(args.tolerance);
	}
	ast_log(LOG_VERBOSE, "Application tiresias. context[%s], durtion[%d], tolerance[%f]\n", context, duration, tolerance);

	ret = ast_channel_state(chan);
	ast_log(LOG_VERBOSE, "Channel state. state[%d]\n", ret);
	if(ret != AST_STATE_UP) {
		/* answer */
		ret = ast_answer(chan);
	}

	/* create temp file */
	tmp = fp_generate_uuid();
	ast_asprintf(&filename, "/tmp/tiresias-%s", tmp);
	sfree(tmp);
	file = 	ast_writefile(filename, "wav", NULL, O_CREAT|O_TRUNC|O_WRONLY, 0, AST_FILE_MODE);
	if(file == NULL) {
		ast_log(LOG_NOTICE, "Could not create temp recording file.\n");
		sfree(filename);
		return -1;
	}

	/* record */
	ret = record_voice(file, chan, duration);
	ast_closestream(file);
	if(ret == 0) {
		pbx_builtin_setvar_helper(chan, "TIRSTATUS", "HANGUP");
		ast_filedelete(filename, NULL);
		sfree(filename);
		return 0;
	}
	else if(ret < 0) {
		pbx_builtin_setvar_helper(chan, "TIRSTATUS", "NOTFOUND");
		ast_filedelete(filename, NULL);
		sfree(filename);
		return 0;
	}

	/* do the fingerprinting and recognition */
	ast_asprintf(&tmp, "%s.wav", filename);
	j_fp = fp_search_fingerprint_info(context, tmp, 1, tolerance);

	/* delete file */
	ast_filedelete(filename, NULL);
	sfree(filename);
	sfree(tmp);

	if(j_fp == NULL) {
		ast_log(LOG_VERBOSE, "Could not get fingerprint info.");
		pbx_builtin_setvar_helper(chan, "TIRSTATUS", "NOTFOUND");
		return 0;
	}

	/* TIRSTATUS */
	pbx_builtin_setvar_helper(chan, "TIRSTATUS", "FOUND");

	/* TIRFRAMECOUNT */
	ret = json_integer_value(json_object_get(j_fp, "frame_count"));
	ast_asprintf(&tmp, "%d", ret);
	pbx_builtin_setvar_helper(chan, "TIRFRAMECOUNT", tmp);
	sfree(tmp);

	/* TIRMATCHCOUNT */
	ret = json_integer_value(json_object_get(j_fp, "match_count"));
	ast_asprintf(&tmp, "%d", ret);
	pbx_builtin_setvar_helper(chan, "TIRMATCHCOUNT", tmp);
	sfree(tmp);

	json_decref(j_fp);

	return 0;
}

/**
 * Record the given channel.
 * @param file
 * @param chan
 * @param duration
 * @return 1:success, 0:hangup, -1:error
 */
static int record_voice(struct ast_filestream* file, struct ast_channel *chan, int duration)
{
	int ret;
	struct timeval start;
	struct ast_frame* frame;
	int ms;
	int err;
	int count;

	if((file == NULL) || (chan == NULL) || (duration < 0)) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return -1;
	}

	err = 0;
	start = ast_tvnow();
	frame = NULL;
	count = 0;
	while(1) {
		ast_log(LOG_DEBUG, "Count[%d]\n", count);
		count++;

		ms = ast_remaining_ms(start, duration);
		ast_log(LOG_DEBUG, "Remaining millisecond. ms[%d]\n", ms);
		if(ms <= 0) {
			break;
		}

		ms = ast_waitfor(chan, ms);
		ast_log(LOG_DEBUG, "Check value. waitfor[%d]\n", ms);
		if(ms < 0) {
			break;
		}

		if((duration > 0) && (ms == 0)) {
			break;
		}

		/* read the frame */
		frame = ast_read(chan);
		if(frame == NULL) {
			/* hangup */
			ast_log(LOG_VERBOSE, "The channel has been hungup.\n");
			err = 1;
			break;
		}
		ast_log(LOG_DEBUG, "Check value. datalen[%d], samples[%d]\n", frame->datalen, frame->samples);

		if(frame->frametype != AST_FRAME_VOICE) {
			ast_log(LOG_DEBUG, "Check frame type. frame_type[%d]\n", frame->frametype);
			ast_frfree(frame);
			continue;
		}

		ret = ast_writestream(file, frame);
		ast_frfree(frame);
		if(ret != 0) {
			ast_log(LOG_WARNING, "Problem writing frame.\n");
			err = 2;
			break;
		}
	}

	if(err == 1) {
		return 0;
	}
	else if(err > 1) {
		return -1;
	}

	return 1;
}

bool application_init(void)
{
	int ret;

	ast_log(LOG_VERBOSE, "init_application_handler.\n");

	ret = ast_register_application2(DEF_APPLICATION_TIRESIAS, tiresias_exec, NULL, NULL, NULL);

	if(ret != 0) {
		return false;
	}

	return true;
}

bool application_term(void)
{
	ast_log(LOG_VERBOSE, "term_application_handler.\n");

	ast_unregister_application(DEF_APPLICATION_TIRESIAS);

	return true;
}

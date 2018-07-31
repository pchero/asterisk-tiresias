/*
 * app_tiresias.c
 *
 *  Created on: Jun 14, 2018
 *      Author: pchero
 */

#define _GNU_SOURCE

#ifndef AST_MODULE
	#define AST_MODULE "tiresias"
#endif

#include <asterisk.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>
#include <asterisk/utils.h>
#include <asterisk/manager.h>
#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/ast_version.h>

#include <stdbool.h>
#include <aubio/aubio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <jansson.h>
#include <dirent.h>

#include "app_tiresias.h"
#include "db_ctx_handler.h"
#include "fp_handler.h"
#include "cli_handler.h"
#include "application_handler.h"

#define DEF_MODULE_NAME	"app_tiresias"

#define DEF_LIB_DIR	"/var/lib/asterisk/third-party/tiresias"

#define DEF_CONFNAME			"tiresias.conf"

#define DEF_CONF_GLOBAL			"global"

#define sfree(p) { if(p != NULL) ast_free(p); p=NULL; }

app* g_app = NULL;

static bool init(void);
static bool init_libdirectory(void);
static bool init_config(void);
static bool init_context(void);
static bool init_audio(void);

static bool term(void);

static bool create_new_audio_info(struct ast_json* j_context);
static bool delete_removed_audio_info(struct ast_json* j_context);


static int file_select(const struct dirent *entry);


static bool init(void)
{
	int ret;

	if(g_app != NULL) {
		ast_json_unref(g_app->j_conf);
		ast_free(g_app);
	}
	g_app = ast_calloc(1, sizeof(app));
	g_app->j_conf = NULL;

	/* initiate lib directory */
	ret = init_libdirectory();
	if(ret != true) {
		ast_log(LOG_ERROR, "Could not initiate libdirectory.\n");
		return false;
	}

	/* initiate configuration */
	ret = init_config();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate configuration options.\n");
		return false;
	}

	/* initiate fp_handler */
	ret = fp_init();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate fp_handler.\n");
		return false;
	}

	ret = init_context();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate context_list.");
		return false;
	}

	ret = init_audio();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate audio_list.\n");
		return false;
	}

	ret = cli_init();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate cli.\n");
		return false;
	}

	ret = application_init();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initate application.\n");
		return false;
	}

	return true;
}

static bool term(void)
{
	int ret;

	ret = fp_term();
	if(ret == false) {
		/* just write the notice log only. */
		ast_log(LOG_NOTICE, "Could not terminate database.\n");
	}

	ret = cli_term();
	if(ret == false) {
		ast_log(LOG_NOTICE, "Could not terminate cli.\n");
	}

	ret = application_term();
	if(ret == false) {
		ast_log(LOG_NOTICE, "Could not terminate application correctly.\n");
	}

	ast_json_unref(g_app->j_conf);
	sfree(g_app);

	return true;
}

static bool init_libdirectory(void)
{
	int ret;
	DIR* dir;

	dir = opendir(DEF_LIB_DIR);
	if(dir != NULL) {
		/* already created */
		closedir(dir);
		return true;
	}

	/* create directory */
	ret = mkdir(DEF_LIB_DIR, 0755);
	if(ret != 0) {
		ast_log(LOG_ERROR, "Could not create lib directory. dir[%s], err[%d:%s]\n", DEF_LIB_DIR, errno, strerror(errno));
		return false;
	}

	return true;
}

/*!
 * \brief Load res_snmp.conf config file
 * \return true on load, false file does not exist
*/
static bool init_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg;
	struct ast_json* j_tmp;
	struct ast_json* j_conf;
	struct ast_flags config_flags = { 0 };
	char *cat;

	j_conf = ast_json_object_create();

	cfg = ast_config_load(DEF_CONFNAME, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Could not load conf file. name[%s]\n", DEF_CONFNAME);
		return false;
	}

	cat = NULL;
	while(1) {
		cat = ast_category_browse(cfg, cat);
		if(cat == NULL) {
			break;
		}

		if(ast_json_object_get(j_conf, cat) == NULL) {
			ast_json_object_set(j_conf, cat, ast_json_object_create());
		}
		j_tmp = ast_json_object_get(j_conf, cat);

		var = ast_variable_browse(cfg, cat);
		while(1) {
			if(var == NULL) {
				break;
			}
			ast_json_object_set(j_tmp, var->name, ast_json_string_create(var->value));
			ast_log(LOG_VERBOSE, "Loading conf. name[%s], value[%s]\n", var->name, var->value);
			var = var->next;
		}
	}
	ast_config_destroy(cfg);

	if(g_app->j_conf != NULL) {
		ast_json_unref(g_app->j_conf);
	}
	g_app->j_conf = j_conf;

	return true;
}

/**
 * Initiate data by configuration settings.
 * @return
 */
static bool init_context(void)
{
	int ret;
	const char* tmp_const;
	const char* name;
	const char* directory;
	int idx;
	struct ast_json* j_contexts;
	struct ast_json* j_context;
	struct ast_json* j_tmp;
	struct ast_json_iter* iter;

	/* validate contexts */
	j_contexts = fp_get_context_lists_all();
	if(j_contexts == NULL) {
		ast_log(LOG_ERROR, "Could not get contexts info.\n");
		return false;
	}

	/* delete the context if the context is not exists in the conf */
	for(idx = 0; idx < ast_json_array_size(j_contexts); idx++) {
		j_context = ast_json_array_get(j_contexts, idx);
		if(j_context == NULL) {
			continue;
		}

		tmp_const = ast_json_string_get(ast_json_object_get(j_context, "name"));
		if(tmp_const == NULL) {
			ast_log(LOG_WARNING, "Could not get context name info.\n");
			continue;
		}

		j_tmp = ast_json_object_get(g_app->j_conf, tmp_const);
		if(j_tmp != NULL) {
			continue;
		}

		/* given context has been deleted already from the configuration. */
		ret = fp_delete_context_list_info(tmp_const);
		if(ret == false) {
			ast_log(LOG_ERROR, "Could not delete context_list info correctly. context[%s]\n", tmp_const);
			continue;
		}

		ast_log(LOG_NOTICE, "Delete context_list info. name[%s]\n", tmp_const);
	}
	ast_json_unref(j_contexts);

	/* create context if not exist */
	iter = ast_json_object_iter(g_app->j_conf);
	while(1) {
		if(iter == NULL) {
			break;
		}

		name = ast_json_object_iter_key(iter);
		j_tmp = ast_json_object_iter_value(iter);

		if(name == NULL) {
			goto next;
		}
		ast_log(LOG_DEBUG, "Checking context info. context[%s]\n", name);

		/* if it's a global context, just continue */
		ret = strcmp(name, DEF_CONF_GLOBAL);
		if(ret == 0) {
			goto next;
		}

		/* get directory info */
		directory = ast_json_string_get(ast_json_object_get(j_tmp, "directory"));
		if(directory == NULL) {
			ast_log(LOG_DEBUG, "Could not get directory option.\n");
			goto next;
		}

		/* create or replace context_list info */
		ret = fp_create_context_list_info(name, directory, true);
		if(ret == false) {
			ast_log(LOG_ERROR, "Could not create or update context_list info. name[%s], directory[%s]", name, directory);
			goto next;
		}

		next:
			iter = ast_json_object_iter_next(g_app->j_conf, iter);
	}

	return true;
}

/**
 * Init audio_list info.
 * @return
 */
static bool init_audio(void)
{
	int ret;
	int idx;
	struct ast_json* j_contexts;
	struct ast_json* j_context;

	// get all context info
	j_contexts = fp_get_context_lists_all();
	if(j_contexts == NULL) {
		ast_log(LOG_NOTICE, "Could not get context_lists info correctly.\n");
		return false;
	}

	for(idx = 0; idx < ast_json_array_size(j_contexts); idx++) {

		j_context = ast_json_array_get(j_contexts, idx);
		if(j_context == NULL) {
			continue;
		}

		ret = delete_removed_audio_info(j_context);
		if(ret == false) {
			ast_log(LOG_WARNING, "Could not delete removed audio info.\n");
		}

		ret = create_new_audio_info(j_context);
		if(ret == false) {
			ast_log(LOG_WARNING, "Could not create new audio info.\n");
		}
	}
	ast_json_unref(j_contexts);

	return true;
}

/**
 * Create new audio info.
 * @param j_context
 * @return
 */
static bool create_new_audio_info(struct ast_json* j_context)
{
	const char* context_name;
	const char* directory;
	int count;
	struct dirent **namelist;
	int i;
	int ret;
	char* tmp;

	if(j_context == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	context_name = ast_json_string_get(ast_json_object_get(j_context, "name"));
	if(context_name == NULL) {
		ast_log(LOG_WARNING, "Could not context name info.\n");
		return false;
	}

	/* get directory info */
	directory = ast_json_string_get(ast_json_object_get(j_context, "directory"));
	if(directory == NULL) {
		ast_log(LOG_VERBOSE, "Could not get directory info. context[%s]\n", context_name);
		return false;
	}

	/* get directory list info */
	count = scandir(directory, &namelist, file_select, alphasort);
	if(count < 0) {
		ast_log(LOG_VERBOSE, "Could not get directory list info. context[%s]\n", context_name);
		return false;
	}

	/* create fingerprint info for each item */
	for(i = 0; i < count; i++) {

		if(namelist[i]->d_name == NULL) {
			ast_log(LOG_ERROR, "Could not get filename.\n");
			continue;
		}

		/* create filename with path*/
		ast_asprintf(&tmp, "%s/%s", directory, namelist[i]->d_name);
		ast_log(LOG_VERBOSE, "Creating fingerprint info. filename[%s]\n", tmp);

		/* create audio_list info */
		ret = fp_craete_audio_list_info(context_name, tmp);
		sfree(namelist[i]);
		sfree(tmp);
		if(ret == false) {
			ast_log(LOG_VERBOSE, "Could not create fingerprint info.\n");
			continue;
		}
	}
	sfree(namelist);

	return true;
}

/**
 * Delete not exist audio info.
 * @param j_context
 * @return
 */
static bool delete_removed_audio_info(struct ast_json* j_context)
{
	struct ast_json* j_audios;
	struct ast_json* j_audio;
	const char* context_name;
	const char* directory;
	int count;
	char* hash;
	char* tmp;
	const char* tmp_const;
	int i;
	int idx;
	int ret;
	int found;
	struct dirent **namelist;


	if(j_context == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	context_name = ast_json_string_get(ast_json_object_get(j_context, "name"));
	if(context_name == NULL) {
		ast_log(LOG_WARNING, "Could not context name info.\n");
		return true;
	}

	/* get audio files list */
	j_audios = fp_get_audio_lists_by_contextname(context_name);
	if(j_audios == NULL) {
		// no delete-able audio files.
		return true;
	}

	/* get directory info */
	directory = ast_json_string_get(ast_json_object_get(j_context, "directory"));
	if(directory == NULL) {
		ast_log(LOG_VERBOSE, "Could not get directory info. context[%s]\n", context_name);
		ast_json_unref(j_audios);
		return true;;
	}

	/* get directory list info */
	count = scandir(directory, &namelist, file_select, alphasort);
	if(count < 0) {
		ast_log(LOG_VERBOSE, "Could not get directory list info. context[%s]\n", context_name);
		ast_json_unref(j_audios);
		return true;
	}

	/* check the audio file is exist */
	for(i = 0; i < count; i++) {

		if(namelist[i]->d_name == NULL) {
			ast_log(LOG_ERROR, "Could not get filename.\n");
			continue;
		}

		/* create filename with path */
		ast_asprintf(&tmp, "%s/%s", directory, namelist[i]->d_name);
		ast_log(LOG_VERBOSE, "Creating hash info. filename[%s]\n", tmp);

		/* create hash info */
		hash = fp_create_hash(tmp);
		if(hash == NULL) {
			ast_log(LOG_ERROR, "Could not create hash info. filename[%s]\n", tmp);
			sfree(tmp);
			continue;
		}

		/* compare audio list */
		found = false;
		for(idx = 0; idx < ast_json_array_size(j_audios); idx++) {
			j_audio = ast_json_array_get(j_audios, idx);
			if(j_audio == NULL) {
				continue;
			}

			tmp_const = ast_json_string_get(ast_json_object_get(j_audio, "hash"));
			if(tmp_const == NULL) {
				ast_log(LOG_DEBUG, "Coould not get hash info.\n");
				continue;
			}

			ret = strcmp(tmp_const, hash);
			if(ret == 0) {
				found = true;
				break;
			}
		}

		/* if found it, remove from the array */
		if(found == true) {
			ast_json_array_remove(j_audios, idx);
		}

		sfree(namelist[i]);
		sfree(tmp);
	}
	sfree(namelist);

	/* delete audio info */
	for(idx = 0; idx < ast_json_array_size(j_audios); idx++) {
		j_audio = ast_json_array_get(j_audios, idx);

		tmp_const = ast_json_string_get(ast_json_object_get(j_audio, "uuid"));
		if(tmp_const == NULL) {
			continue;
		}

		ret = fp_delete_audio_list_info(tmp_const);
		if(ret == false) {
			ast_log(LOG_DEBUG, "Could not delete audio list info.\n");
			continue;
		}
	}
	ast_json_unref(j_audios);

	return true;
}

static int file_select(const struct dirent *entry)
{
	int ret;

	if((entry == NULL) || (entry->d_name == NULL)) {
		return 0;
	}

	ret = strcmp(entry->d_name, ".");
	if(ret == 0) {
		return 0;
	}

	ret = strcmp(entry->d_name, "..");
	if(ret == 0) {
		return 0;
	}

	return 1;
}


/**
 * @brief Load module
 * @return
 */
static int load_module(void)
{
	int ret;

	ast_log(LOG_NOTICE, "Load %s.\n", DEF_MODULE_NAME);

	ret = init();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate the module.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int ret;

	ast_log(LOG_NOTICE, "Unload %s.\n", DEF_MODULE_NAME);

	ret = term();
	if(ret == false) {
		ast_log(LOG_NOTICE, "Could not terminate the module correctly.\n");
	}

	return 0;
}

static int reload_module(void)
{
	ast_log(LOG_NOTICE, "%s doesn't support reload. Please do unload/load manually.\n",
			DEF_MODULE_NAME
			);
	return AST_MODULE_RELOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Tiresias",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_DEFAULT,
		.support_level = AST_MODULE_SUPPORT_CORE,
		);

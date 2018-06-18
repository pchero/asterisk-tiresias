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

static int file_select(const struct dirent *entry);


static bool init(void)
{
	int ret;

	if(g_app != NULL) {
		json_decref(g_app->j_conf);
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

	json_decref(g_app->j_conf);
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
	json_t* j_tmp;
	json_t* j_conf;
	struct ast_flags config_flags = { 0 };
	char *cat;

	j_conf = json_object();

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

		if(json_object_get(j_conf, cat) == NULL) {
			json_object_set_new(j_conf, cat, json_object());
		}
		j_tmp = json_object_get(j_conf, cat);

		var = ast_variable_browse(cfg, cat);
		while(1) {
			if(var == NULL) {
				break;
			}
			json_object_set_new(j_tmp, var->name, json_string(var->value));
			ast_log(LOG_VERBOSE, "Loading conf. name[%s], value[%s]\n", var->name, var->value);
			var = var->next;
		}
	}
	ast_config_destroy(cfg);

	if(g_app->j_conf != NULL) {
		json_decref(g_app->j_conf);
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
	json_t* j_contexts;
	json_t* j_context;
	json_t* j_tmp;

	/* validate contexts */
	j_contexts = fp_get_context_lists_all();
	if(j_contexts == NULL) {
		ast_log(LOG_ERROR, "Could not get contexts info.\n");
		return false;
	}

	/* delete the context if the context is not exists in the conf */
	json_array_foreach(j_contexts, idx, j_context) {
		tmp_const = json_string_value(json_object_get(j_context, "name"));
		if(tmp_const == NULL) {
			ast_log(LOG_WARNING, "Could not get context name info.\n");
			continue;
		}

		j_tmp = json_object_get(g_app->j_conf, tmp_const);
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
	json_decref(j_contexts);

	/* create context if not exist */
	json_object_foreach(g_app->j_conf, name, j_tmp) {
		if(name == NULL) {
			continue;
		}
		ast_log(LOG_DEBUG, "Checking context info. context[%s]\n", name);

		/* if it's a global context, just continue */
		ret = strcmp(name, DEF_CONF_GLOBAL);
		if(ret == 0) {
			continue;
		}

		/* get directory info */
		directory = json_string_value(json_object_get(j_tmp, "directory"));
		if(directory == NULL) {
			ast_log(LOG_DEBUG, "Could not get directory option.\n");
			continue;
		}

		/* create or replace context_list info */
		ret = fp_create_context_list_info(name, directory, true);
		if(ret == false) {
			ast_log(LOG_ERROR, "Could not create or update context_list info. name[%s], directory[%s]", name, directory);
			continue;
		}
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
	int i;
	int count;
    struct dirent **namelist;
    char* tmp;
	const char* context_name;
	const char* directory;
	json_t* j_contexts;
	json_t* j_context;
	int idx;

	j_contexts = fp_get_context_lists_all();
	if(j_contexts == NULL) {
		ast_log(LOG_NOTICE, "Could not get context_lists info correctly.\n");
		return false;
	}

	json_array_foreach(j_contexts, idx, j_context) {
		context_name = json_string_value(json_object_get(j_context, "name"));
		if(context_name == NULL) {
			ast_log(LOG_WARNING, "Could not context name info.\n");
			continue;
		}

		/* get directory info */
		directory = json_string_value(json_object_get(j_context, "directory"));
		if(directory == NULL) {
			ast_log(LOG_VERBOSE, "Could not get directory info. context[%s]\n", context_name);
			continue;
		}

		/* get directory list info */
		count = scandir(directory, &namelist, file_select, alphasort);
		if(count < 0) {
			ast_log(LOG_VERBOSE, "Could not get directory list info. context[%s]\n", context_name);
			continue;
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
	}

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

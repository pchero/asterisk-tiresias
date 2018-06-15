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
#include <asterisk/cli.h>
#include <asterisk/utils.h>
#include <asterisk/manager.h>
#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/ast_version.h>
#include <asterisk/json.h>

#include <stdbool.h>

#define DEF_MODULE_NAME	"app_tiresias"

static PyObject* g_pymod_json;
static PyObject* g_pymod_dejavu;
static PyObject* g_pymod_dejavu_recog;

static bool init_python(void);
static bool term_python(void);


/**
 * Initiate python module
 * @return
 */
static bool init_python(void)
{
//	putenv("PYTHONPATH=$PYTHONPATH:/usr/local/lib/python2.7/dist-packages");

	// initiate python
	Py_Initialize();

	// import json
  g_pymod_json = PyImport_ImportModule("json");
  if(g_pymod_json == NULL) {
  	ast_log(LOG_ERROR, "Could not import module. json\n");
    return false;
  }


  // import dejavu
  g_pymod_dejavu = PyImport_ImportModule("dejavu");
  if(g_pymod_dejavu == NULL) {
  	ast_log(LOG_ERROR, "Could not import module. dejavu\n");
    return false;
  }

  // import dejavu.recognize
  g_pymod_dejavu_recog = PyImport_ImportModule("dejavu.recognize");
  if(g_pymod_dejavu_recog == NULL) {
  	ast_log(LOG_ERROR, "Could not import module. dejavu.recognize\n");
    return false;
  }

  return true;
}

/**
 * Terminate python module.
 */
static bool term_python(void)
{
//	if(g_pymod_dejavu != NULL) {
//		Py_DECREF(g_pymod_dejavu);
//	}
//
//	if(g_pymod_dejavu_recog != NULL) {
//		Py_DECREF(g_pymod_dejavu_recog);
//	}
//
//	if(g_pymod_json != NULL) {
//		Py_DECREF(g_pymod_json);
//	}

  Py_Finalize();

  return true;
}


/**
 * @brief Load module
 * @return
 */
static int load_module(void)
{
	int ret;

	ast_log(LOG_NOTICE, "Load %s.\n", DEF_MODULE_NAME);

	ret = init_python();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate python.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int ret;

	ast_log(LOG_NOTICE, "Unload %s.\n", DEF_MODULE_NAME);

	ret = term_python();
	if(ret == false) {
		ast_log(LOG_NOTICE, "Could not terminate python.\n");
	}

	return 0;
}

static int reload_module(void)
{
	ast_log(LOG_NOTICE, "%s doesn't support reload. Please do unload/load manualy.\n",
			DEF_MODULE_NAME
			);
	return AST_MODULE_RELOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Voicefingerprint",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_DEFAULT,
		.support_level = AST_MODULE_SUPPORT_CORE,
		);

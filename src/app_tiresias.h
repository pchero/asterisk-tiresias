/*
 * app_tiresias.h
 *
 *  Created on: Jun 16, 2018
 *      Author: pchero
 */

#ifndef SRC_APP_TIRESIAS_H_
#define SRC_APP_TIRESIAS_H_

#include <asterisk/json.h>

typedef struct _app {
	struct ast_json*   j_conf;	///< config
} app;

extern app* g_app;

#endif /* SRC_APP_TIRESIAS_H_ */

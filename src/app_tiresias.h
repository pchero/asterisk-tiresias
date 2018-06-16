/*
 * app_tiresias.h
 *
 *  Created on: Jun 16, 2018
 *      Author: pchero
 */

#ifndef SRC_APP_TIRESIAS_H_
#define SRC_APP_TIRESIAS_H_

#include <jansson.h>

typedef struct _app {
	json_t*   j_conf;	///< config
} app;

extern app* g_app;

#endif /* SRC_APP_TIRESIAS_H_ */

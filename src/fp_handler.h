/*
 * fp_handler.h
 *
 *  Created on: Jun 10, 2018
 *      Author: pchero
 */

#ifndef FP_HANDLER_H_
#define FP_HANDLER_H_

#include <asterisk/json.h>

bool fp_init(void);
bool fp_term(void);

bool fp_create_context_list_info(const char* name, const char* directory, bool replace);
bool fp_delete_context_list_info(const char* name);
struct ast_json* fp_get_context_lists_all(void);
struct ast_json* fp_get_context_list_info(const char* name);


struct ast_json* fp_get_audio_lists_all(void);
struct ast_json* fp_get_audio_lists_by_contextname(const char* name);

bool fp_craete_audio_list_info(const char* context, const char* filename);
bool fp_delete_audio_list_info(const char* uuid);

struct ast_json* fp_search_fingerprint_info(
		const char* context,
		const char* filename,
		const int coefs,
		const double tolerance,
		const int freq_ignore_low,
		const int freq_ignore_high
		);

char* fp_generate_uuid(void);

#endif /* FP_HANDLER_H_ */

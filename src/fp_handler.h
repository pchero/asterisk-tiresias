/*
 * fp_handler.h
 *
 *  Created on: Jun 10, 2018
 *      Author: pchero
 */

#ifndef FP_HANDLER_H_
#define FP_HANDLER_H_

bool fp_init(void);
bool fp_term(void);

bool fp_create_context_list_info(const char* name);
bool fp_delete_context_list_info(const char* name);
json_t* fp_get_context_lists_all(void);
json_t* fp_get_context_list_info(const char* name);


json_t* fp_get_audio_lists_all(void);
json_t* fp_get_audio_lists_by_contextname(const char* name);

bool fp_delete_audio_list_info(const char* uuid);
bool fp_craete_audio_list_info(const char* filename);

json_t* fp_search_fingerprint_info(const char* filename, const int coefs);

#endif /* FP_HANDLER_H_ */

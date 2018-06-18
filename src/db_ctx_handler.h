/*
 * db_ctx_handler.h
 *
 *  Created on: Jun 9, 2018
 *      Author: pchero
 */

#ifndef DB_CTX_HANDLER_H_
#define DB_CTX_HANDLER_H_

#include <sqlite3.h>
#include <stdbool.h>
#include <jansson.h>

typedef struct _db_ctx_t
{
  struct sqlite3* db;

  struct sqlite3_stmt* stmt;
} db_ctx_t;

db_ctx_t* db_ctx_init(const char* name);
void db_ctx_term(db_ctx_t* ctx);

bool db_ctx_exec(db_ctx_t* ctx, const char* query);
bool db_ctx_query(db_ctx_t* ctx, const char* query);
json_t* db_ctx_get_record(db_ctx_t* ctx);

bool db_ctx_insert(db_ctx_t* ctx, const char* table, const json_t* j_data);
bool db_ctx_insert_or_replace(db_ctx_t* ctx, const char* table, const json_t* j_data);

char* db_ctx_get_update_str(const json_t* j_data);
char* db_ctx_get_condition_str(const json_t* j_data);

bool db_ctx_backup(db_ctx_t* ctx, const char *filename);

bool db_ctx_load_db_all(db_ctx_t* ctx, const char* filename);
bool db_ctx_load_db_schema(db_ctx_t* ctx, const char* filename);
bool db_ctx_load_db_data(db_ctx_t* ctx, const char* filename);

bool db_ctx_update_item(db_ctx_t* ctx, const char* table, const char* key_column, const json_t* j_data);


bool db_ctx_free(db_ctx_t* ctx);




#endif /* DB_CTX_HANDLER_H_ */

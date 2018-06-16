/*
 * db_handler.c
 *
 *  Created on: Jun 9, 2018
 *      Author: pchero
 */

#define _GNU_SOURCE

#include <asterisk.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>

#include "db_ctx_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define sfree(p) { if(p != NULL) ast_free(p); p=NULL; }


typedef struct {
  int max_retry;  /* Max retry times. */
  int sleep_ms;   /* Time to sleep before retry again. */
} busy_handler_attr;


static bool db_ctx_connect(db_ctx_t* ctx, const char* filename);
static db_ctx_t* db_ctx_create(void);
static int db_ctx_busy_handler(void *data, int retry);
static bool db_ctx_insert_basic(db_ctx_t* ctx, const char* table, const json_t* j_data, int replace);

static int process_ddl_row(void* pData, int nColumns, char** values, char** columns);
static int process_dml_row(void *pData, int nColumns, char **values, char **columns);


static db_ctx_t* db_ctx_create(void)
{
  db_ctx_t* ctx;

  ctx = calloc(1, sizeof(db_ctx_t));
  if(ctx == NULL) {
    ast_log(LOG_ERROR, "Could not create new db context.");
    return NULL;
  }
  ctx->db = NULL;
  ctx->stmt = NULL;

  return ctx;
}

/**
 Connect to db.

 @return Success:TRUE, Fail:FALSE
 */
static bool db_ctx_connect(db_ctx_t* ctx, const char* filename)
{
  int ret;

  if((ctx == NULL) || (filename == NULL)) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  if(ctx->db != NULL) {
    ast_log(AST_LOG_NOTICE, "Database is already connected.");
    return true;
  }

  ret = sqlite3_open(filename, &ctx->db);
  if(ret != SQLITE_OK) {
    ast_log(LOG_ERROR, "Could not initiate database. err[%s]", sqlite3_errmsg(ctx->db));
    return false;
  }
  ast_log(LOG_DEBUG, "Connected to database ctx. filename[%s]", filename);

  return true;
}

/**
 * free ctx stmt
 * @param ctx
 */
bool db_ctx_free(db_ctx_t* ctx)
{
  int ret;

  if(ctx == NULL) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
  }

  // check already freed
  if(ctx->stmt == NULL) {
    return true;
  }

  ret = sqlite3_finalize(ctx->stmt);
  if(ret != SQLITE_OK) {
    ast_log(LOG_ERROR, "Could not finalize stme. ret[%d]", ret);
    return false;
  }
  ctx->stmt = NULL;

  return true;
}

/*
 * Busy callback handler for all operations. If the busy callback handler is
 * NULL, then SQLITE_BUSY or SQLITE_IOERR_BLOCKED is returned immediately upon
 * encountering the lock. If the busy callback is not NULL, then the callback
 * might be invoked.
 *
 * This callback will be registered by SQLite's API:
 *    int sqlite3_busy_handler(sqlite3*, int(*)(void*,int), void*);
 * That's very useful to deal with SQLITE_BUSY event automatically. Otherwise,
 * you have to check the return code, reset statement and do retry manually.
 *
 * We've to use ANSI C declaration here to eliminate warnings in Visual Studio.
 */
static int db_ctx_busy_handler(void *data, int retry)
{
  busy_handler_attr* attr;

  attr = (busy_handler_attr*)data;

  if (retry < attr->max_retry) {
    /* Sleep a while and retry again. */
    ast_log(LOG_DEBUG, "Hits SQLITE_BUSY %d times, retry again.", retry);
    sqlite3_sleep(attr->sleep_ms);

    /* Return non-zero to let caller retry again. */
    return 1;
  }

  /* Return zero to let caller return SQLITE_BUSY immediately. */
  ast_log(LOG_ERROR, "Retried too many, exit. retry[%d]", retry);
  return 0;
}


/**
 * Init database
 * @return
 */
db_ctx_t* db_ctx_init(const char* name)
{
  int ret;
  db_ctx_t* db_ctx;

  if(name == NULL) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  ast_log(LOG_DEBUG, "Initiate db context. name[%s]", name);

  // create new db_ctx
  db_ctx = db_ctx_create();
  if(db_ctx == NULL) {
    ast_log(LOG_ERROR, "Could not create db context.");
    return NULL;
  }

  // connect db
  ret = db_ctx_connect(db_ctx, name);
  if(ret == false) {
    sfree(db_ctx);
    return NULL;
  }

  return db_ctx;
}

/**
 Disconnect to db.
 */
void db_ctx_term(db_ctx_t* ctx)
{
  int ret;

  if(ctx == NULL) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  db_ctx_free(ctx);

  ret = sqlite3_close(ctx->db);
  if(ret != SQLITE_OK) {
    ast_log(LOG_ERROR, "Could not close the database correctly. err[%s]", sqlite3_errmsg(ctx->db));
    return;
  }

  ast_log(LOG_DEBUG, "Released database context.");
  ctx->db = NULL;

  sfree(ctx);
}

/**
 database query function. (select)
 @param query
 @return Success:, Fail:NULL
 */
bool db_ctx_query(db_ctx_t* ctx, const char* query)
{
  int ret;
  sqlite3_stmt* result;

  if((ctx == NULL) || (query == NULL) || (ctx->db == NULL)) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // free ctx stmt if exists
  db_ctx_free(ctx);

  ret = sqlite3_prepare_v2(ctx->db, query, -1, &result, NULL);
  if(ret != SQLITE_OK) {
    ast_log(LOG_ERROR, "Could not prepare query. query[%s], err[%s]", query, sqlite3_errmsg(ctx->db));
    return false;
  }

  ctx->stmt = result;
  return true;
}

/**
 * database query execute function. (update, delete, insert)
 * @param query
 * @return  success:true, fail:false
 */
bool db_ctx_exec(db_ctx_t* ctx, const char* query)
{
  int ret;
  char* err;
  busy_handler_attr bh_attr;

  if((ctx == NULL) || (query == NULL) || (ctx->db == NULL)) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  /* Setup busy handler for all following operations. */
  bh_attr.max_retry = 100;  /* Max retry times */
  bh_attr.sleep_ms = 100;   /* Sleep 100ms before each retry */
  sqlite3_busy_handler(ctx->db, db_ctx_busy_handler, &bh_attr);

  // execute
  ret = sqlite3_exec(ctx->db, query, NULL, 0, &err);
  if(ret != SQLITE_OK) {
    ast_log(LOG_ERROR, "Could not execute query. query[%s], err[%s]", query, err);
    sqlite3_free(err);
    return false;
  }
  sqlite3_free(err);

  return true;
}

/**
 * Return 1 record info by json.
 * If there's no more record or error happened, it will return NULL.
 * @param res
 * @return  success:json_t*, fail:NULL
 */
json_t* db_ctx_get_record(db_ctx_t* ctx)
{
	int ret;
	int cols;
	int i;
	json_t* j_res;
	json_t* j_tmp;
	int type;
	const char* tmp_const;

  if(ctx == NULL) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

	ret = sqlite3_step(ctx->stmt);
	if(ret != SQLITE_ROW) {
		if(ret != SQLITE_DONE) {
		  ast_log(LOG_ERROR, "Could not patch the result. ret[%d], err[%s]", ret, sqlite3_errmsg(ctx->db));
		}
		return NULL;
	}

	cols = sqlite3_column_count(ctx->stmt);
	j_res = json_object();
	for(i = 0; i < cols; i++) {
		j_tmp = NULL;
		type = sqlite3_column_type(ctx->stmt, i);
		switch(type) {
			case SQLITE_INTEGER: {
				j_tmp = json_integer(sqlite3_column_int(ctx->stmt, i));
			}
			break;

			case SQLITE_FLOAT: {
				j_tmp = json_real(sqlite3_column_double(ctx->stmt, i));
			}
			break;

			case SQLITE_NULL: {
				j_tmp = json_null();
			}
			break;

			case SQLITE3_TEXT:
			{
			  // if the text is loadable, create json object.
			  tmp_const = (const char*)sqlite3_column_text(ctx->stmt, i);
			  if(tmp_const == NULL) {
			    j_tmp = json_null();
			  }
			  else {
	        j_tmp = json_loads(tmp_const, JSON_DECODE_ANY, NULL);
	        if(j_tmp == NULL) {
	          j_tmp = json_string((const char*)sqlite3_column_text(ctx->stmt, i));
	        }
	        else {
	          // check type
            // the only array/object/string types are allowed
	          // especially, we don't allow the JSON_NULL type at this point.
	          // Cause the json_loads() consider the "null" string to JSON_NULL.
	          // It's should be done at the above.
            ret = json_typeof(j_tmp);
            if((ret != JSON_ARRAY) && (ret != JSON_OBJECT) && (ret != JSON_STRING)) {
              json_decref(j_tmp);
              j_tmp = json_string((const char*)sqlite3_column_text(ctx->stmt, i));
            }
	        }
			  }
			}
			break;

			case SQLITE_BLOB:
			default:
			{
				// not done yet.
			  ast_log(LOG_NOTICE, "Not supported type. type[%d]", type);
				j_tmp = json_null();
			}
			break;
		}

		if(j_tmp == NULL) {
		  ast_log(LOG_ERROR, "Could not parse result column. name[%s], type[%d]",
					sqlite3_column_name(ctx->stmt, i), type);
			j_tmp = json_null();
		}
		json_object_set_new(j_res, sqlite3_column_name(ctx->stmt, i), j_tmp);
	}

	return j_res;
}

/**
 * Insert j_data into table.
 * @param table
 * @param j_data
 * @return
 */
bool db_ctx_insert(db_ctx_t* ctx, const char* table, const json_t* j_data)
{
  int ret;

  if((ctx == NULL) || (table == NULL) || (j_data == NULL)) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
//  ast_log(LOG_DEBUG, "Fired db_ctx_insert");

  ret = db_ctx_insert_basic(ctx, table, j_data, false);
  if(ret == false) {
    ast_log(LOG_ERROR, "Could not insert data.");
    return false;
  }

  return true;
}

/**
 * Insert or replace j_data into table.
 * @param table
 * @param j_data
 * @return
 */
bool db_ctx_insert_or_replace(db_ctx_t* ctx, const char* table, const json_t* j_data)
{
  int ret;

  if((ctx == NULL) || (ctx->db == NULL) || (table == NULL) || (j_data == NULL)) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
//  ast_log(LOG_DEBUG, "Fired db_ctx_insert_or_replace");

  ret = db_ctx_insert_basic(ctx, table, j_data, true);
  if(ret == false) {
    ast_log(LOG_ERROR, "Could not insert data.");
    return false;
  }

  return true;
}

/**
 * Insert j_data into table.
 * @param table
 * @param j_data
 * @return
 */
static bool db_ctx_insert_basic(db_ctx_t* ctx, const char* table, const json_t* j_data, int replace)
{
  char* sql;
  json_t*     j_data_cp;
  char*       tmp;
  const char* key;
  json_t*     j_val;
  int         ret;
  json_type   type;
  char*       sql_keys;
  char*       sql_values;
  char*       tmp_sub;
  char*       tmp_sqlite_buf; // sqlite3_mprintf

  if((ctx == NULL) || (ctx->db == NULL) || (table == NULL) || (j_data == NULL)) {
    ast_log(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
//  ast_log(LOG_DEBUG, "db_ctx_insert_basic.");

  // copy original.
  j_data_cp = json_deep_copy(j_data);

  tmp = NULL;
  sql_keys  = NULL;
  sql_values = NULL;
  tmp_sub = NULL;
  json_object_foreach(j_data_cp, key, j_val) {

    // key
    if(sql_keys == NULL) {
      asprintf(&tmp, "%s", key);
    }
    else {
      asprintf(&tmp, "%s, %s", sql_keys, key);
    }
    sfree(sql_keys);
    asprintf(&sql_keys, "%s", tmp);
    sfree(tmp);

    // value
    sfree(tmp_sub);

    // get type.
    type = json_typeof(j_val);
    switch(type) {
      // string
      case JSON_STRING: {
        asprintf(&tmp_sub, "\'%s\'", json_string_value(j_val));
      }
      break;

      // numbers
      case JSON_INTEGER: {
        asprintf(&tmp_sub, "%lld", json_integer_value(j_val));
      }
      break;

      case JSON_REAL: {
        asprintf(&tmp_sub, "%f", json_real_value(j_val));
      }
      break;

      // true
      case JSON_TRUE: {
        asprintf(&tmp_sub, "\"%s\"", "true");
      }
      break;

      // false
      case JSON_FALSE: {
        asprintf(&tmp_sub, "\"%s\"", "false");
      }
      break;

      case JSON_NULL: {
        asprintf(&tmp_sub, "%s", "null");
      }
      break;

      case JSON_ARRAY:
      case JSON_OBJECT: {
        tmp = json_dumps(j_val, JSON_ENCODE_ANY);
        tmp_sqlite_buf = sqlite3_mprintf("%q", tmp);
        sfree(tmp);

        asprintf(&tmp_sub, "'%s'", tmp_sqlite_buf);
        sqlite3_free(tmp_sqlite_buf);
      }
      break;

      // object
      // array
      default: {
        // Not done yet.

        // we don't support another types.
        ast_log(LOG_WARNING, "Wrong type input. We don't handle this.");
        asprintf(&tmp_sub, "\"%s\"", "null");
      }
      break;
    }

    if(sql_values == NULL) {
      asprintf(&tmp, "%s", tmp_sub);
    }
    else {
      asprintf(&tmp, "%s, %s", sql_values, tmp_sub);
    }
    sfree(tmp_sub);
    sfree(sql_values);
    sql_values = strdup(tmp);
    sfree(tmp);
  }
  json_decref(j_data_cp);

  if(replace == true) {
    asprintf(&sql, "insert or replace into %s(%s) values (%s);", table, sql_keys, sql_values);
  }
  else {
    asprintf(&sql, "insert into %s(%s) values (%s);", table, sql_keys, sql_values);
  }
  sfree(sql_keys);
  sfree(sql_values);

  ret = db_ctx_exec(ctx, sql);
  sfree(sql);
  if(ret == false) {
    ast_log(LOG_ERROR, "Could not insert data.");
    return false;
  }

  return true;
}

/**
 * Return part of update sql.
 * @param j_data
 * @return
 */
char* db_ctx_get_update_str(const json_t* j_data)
{
  char*   res;
  char*   tmp;
  char*   tmp_sub;
  char*   tmp_sqlite_buf;
  json_t*  j_val;
  json_t*  j_data_cp;
  const char* key;
  bool        is_first;
  json_type   type;

  // copy original data.
  j_data_cp = json_deep_copy(j_data);

  is_first = true;
  res = NULL;
  tmp = NULL;
  tmp_sub = NULL;

  json_object_foreach(j_data_cp, key, j_val) {

    // create update string
    type = json_typeof(j_val);
    switch(type) {
      // string
      case JSON_STRING: {
        asprintf(&tmp_sub, "%s = \'%s\'", key, json_string_value(j_val));
      }
      break;

      // numbers
      case JSON_INTEGER: {
        asprintf(&tmp_sub, "%s = %lld", key, json_integer_value(j_val));
      }
      break;

      case JSON_REAL: {
        asprintf(&tmp_sub, "%s = %lf", key, json_real_value(j_val));
      }
      break;

      // true
      case JSON_TRUE: {
        asprintf(&tmp_sub, "%s = \"%s\"", key, "true");
      }
      break;

      // false
      case JSON_FALSE: {
        asprintf(&tmp_sub, "%s = \"%s\"", key, "false");
      }
      break;

      case JSON_NULL: {
        asprintf(&tmp_sub, "%s = %s", key, "null");
      }
      break;

      case JSON_ARRAY:
      case JSON_OBJECT: {
        tmp = json_dumps(j_val, JSON_ENCODE_ANY);
        tmp_sqlite_buf = sqlite3_mprintf("%q", tmp);
        sfree(tmp);

        asprintf(&tmp_sub, "%s = '%s'", key, tmp_sqlite_buf);
        sqlite3_free(tmp_sqlite_buf);
      }
      break;

      default: {
        // Not done yet.
        // we don't support another types.
        ast_log(LOG_WARNING, "Wrong type input. We don't handle this.");
        asprintf(&tmp_sub, "%s = %s", key, "null");
      }
      break;
    }

    // copy/set previous sql.
    sfree(tmp);
    if(is_first == true) {
      asprintf(&tmp, "%s", tmp_sub);
      is_first = false;
    }
    else {
      asprintf(&tmp, "%s, %s", res, tmp_sub);
    }
    sfree(res);
    sfree(tmp_sub);

    res = strdup(tmp);
    sfree(tmp);
  }
  json_decref(j_data_cp);

  return res;
}

/*
 * Return the condition string for given data.
 */
char* db_ctx_get_condition_str(const json_t* j_data)
{
  char*   res;
  char*   tmp;
  char*   tmp_sub;
  char*   tmp_sqlite_buf;
  json_t*  j_val;
  json_t*  j_data_cp;
  const char* key;
  bool        is_first;
  json_type   type;

  // copy original data.
  j_data_cp = json_deep_copy(j_data);

  is_first = true;
  res = NULL;
  tmp = NULL;
  tmp_sub = NULL;

  json_object_foreach(j_data_cp, key, j_val) {

    // create update string
    type = json_typeof(j_val);
    switch(type) {
      // string
      case JSON_STRING: {
        asprintf(&tmp_sub, "%s = \'%s\'", key, json_string_value(j_val));
      }
      break;

      // numbers
      case JSON_INTEGER: {
        asprintf(&tmp_sub, "%s = %lld", key, json_integer_value(j_val));
      }
      break;

      case JSON_REAL: {
        asprintf(&tmp_sub, "%s = %lf", key, json_real_value(j_val));
      }
      break;

      // true
      case JSON_TRUE: {
        asprintf(&tmp_sub, "%s = \"%s\"", key, "true");
      }
      break;

      // false
      case JSON_FALSE: {
        asprintf(&tmp_sub, "%s = \"%s\"", key, "false");
      }
      break;

      case JSON_NULL: {
        asprintf(&tmp_sub, "%s = %s", key, "null");
      }
      break;

      case JSON_ARRAY:
      case JSON_OBJECT: {
        tmp = json_dumps(j_val, JSON_ENCODE_ANY);
        tmp_sqlite_buf = sqlite3_mprintf("%q", tmp);
        sfree(tmp);

        asprintf(&tmp_sub, "%s = '%s'", key, tmp_sqlite_buf);
        sqlite3_free(tmp_sqlite_buf);
      }
      break;

      default: {
        // Not done yet.
        // we don't support another types.
        ast_log(LOG_WARNING, "Wrong type input. We don't handle this.");
        asprintf(&tmp_sub, "%s = %s", key, "null");
      }
      break;
    }

    // copy/set previous sql.
    sfree(tmp);
    if(is_first == true) {
      asprintf(&tmp, "%s", tmp_sub);
      is_first = false;
    }
    else {
      asprintf(&tmp, "%s and %s", res, tmp_sub);
    }
    sfree(res);
    sfree(tmp_sub);

    res = strdup(tmp);
    sfree(tmp);
  }
  json_decref(j_data_cp);

  return res;
}

bool db_ctx_backup(db_ctx_t* ctx, const char *filename)
{
  int ret;
  sqlite3* db;
  sqlite3_backup* backup;

  if((ctx == NULL) || (filename == NULL)) {
  	ast_log(LOG_WARNING, "Wrong input parameter.");
  	return false;
  }

  /* Open the database file identified by zFilename. */
  ret = sqlite3_open(filename, &db);
  if(ret != SQLITE_OK) {
  	return false;
  }

  backup = sqlite3_backup_init(db, "main", ctx->db, "main");
  if(backup == NULL) {
  	ast_log(LOG_WARNING, "Could not initiate backup database.");
  	return false;
  }

  while(1) {
  	ret = sqlite3_backup_step(backup, 5);
  	if(ret == SQLITE_DONE) {
  		break;
  	}

  	if((ret != SQLITE_OK) && (ret != SQLITE_BUSY) && (ret != SQLITE_LOCKED)) {
  		ast_log(LOG_ERROR, "Could not backup the database. ret[%d]", ret);
  		break;
  	}

  	// if database is busy or locked, just sleep.
  	if((ret == SQLITE_BUSY) || (ret == SQLITE_LOCKED)) {
  		sqlite3_sleep(250);
  	}
  }

  sqlite3_backup_finish(backup);
  sqlite3_close(db);

  return true;
}

bool db_ctx_load_db_schema(db_ctx_t* ctx, const char* filename)
{
  int ret;
  sqlite3* budb;

  if((ctx == NULL) || (filename == NULL)) {
  	ast_log(LOG_WARNING, "Wrong input parameter.");
  	return false;
  }

  ret = sqlite3_open(filename, &budb);
  if(ret != SQLITE_OK) {
  	ast_log(LOG_ERROR, "Could not open the database.");
  	return false;
  }

  // Create the in-memory schema from the backup
  sqlite3_exec(budb, "BEGIN", NULL, NULL, NULL);
  sqlite3_exec(budb, "SELECT sql FROM sqlite_master WHERE sql NOT NULL", &process_ddl_row, ctx->db, NULL);
  sqlite3_exec(budb, "COMMIT", NULL, NULL, NULL);
  sqlite3_close(budb);

  return true;
}

/**
 * Load data from given filename to ctx.
 * @param ctx
 * @param filename
 * @return
 */
bool db_ctx_load_db_data(db_ctx_t* ctx, const char* filename)
{
  char* sql;

  if((ctx == NULL) || (filename == NULL)) {
  	ast_log(LOG_WARNING, "Wrong input parameter.");
  	return false;
  }

  // Attach the backup to the in memory
  sql = sqlite3_mprintf("ATTACH DATABASE '%s' as backup", filename);
  sqlite3_exec(ctx->db, sql, NULL, NULL, NULL);
  sqlite3_free(sql);

  // Copy the data from the backup to the in memory
  sqlite3_exec(ctx->db, "BEGIN", NULL, NULL, NULL);
  sqlite3_exec(ctx->db, "SELECT name FROM backup.sqlite_master WHERE type='table'", &process_dml_row, ctx->db, NULL);
  sqlite3_exec(ctx->db, "COMMIT", NULL, NULL, NULL);

  sqlite3_exec(ctx->db, "DETACH DATABASE backup", NULL, NULL, NULL);

  return true;
}

/**
 * Load given database into the given ctx.
 * @param ctx
 * @param filename
 * @return
 */
bool db_ctx_load_db_all(db_ctx_t* ctx, const char* filename)
{
  int ret;

  if((ctx == NULL) || (filename == NULL)) {
  	ast_log(LOG_WARNING, "Wrong input parameter.");
  	return false;
  }

  // load schema
  ret = db_ctx_load_db_schema(ctx, filename);
  if(ret == false) {
    ast_log(LOG_ERROR, "Could not load db scema.");
    return false;
  }

  // load data
  ret = db_ctx_load_db_data(ctx, filename);
  if(ret == false) {
    ast_log(LOG_ERROR, "Could not load db scema.");
    return false;
  }

  return true;
}

/**
 * Exec an sql statement in values[0] against
 * the database in pData.
 */
static int process_ddl_row(void* pData, int nColumns, char** values, char** columns)
{
  if (nColumns != 1) {
    return 1; // Error
  }

  sqlite3* db = (sqlite3*)pData;
  sqlite3_exec(db, values[0], NULL, NULL, NULL);

  return 0;
}

/**
 * Insert from a table named by backup.{values[0]}
 * into main.{values[0]} in database pData.
 */
static int process_dml_row(void *pData, int nColumns, char **values, char **columns)
{
  if (nColumns != 1) {
    return 1; // Error
  }

  sqlite3* db = (sqlite3*)pData;

  char *stmt = sqlite3_mprintf("insert into main.%q select * from backup.%q", values[0], values[0]);
  sqlite3_exec(db, stmt, NULL, NULL, NULL);
  sqlite3_free(stmt);

  return 0;
}


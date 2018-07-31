/*
 * fp_handler.c
 *
 *  Created on: Jun 10, 2018
 *      Author: pchero
 */

#define _GNU_SOURCE

#include <asterisk.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>
#include <asterisk/json.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <aubio/aubio.h>
#include <math.h>
#include <openssl/md5.h>
#include <libgen.h>
#include <uuid/uuid.h>

#include "app_tiresias.h"
#include "db_ctx_handler.h"
#include "fp_handler.h"

#define sfree(p) { if(p != NULL) ast_free(p); p=NULL; }

#define DEF_DATABASE_NAME			":memory:"
#define DEF_BACKUP_DATABASE			"/var/lib/asterisk/third-party/tiresias/audio_recongition.db"

#define DEF_AUBIO_HOPSIZE		256
#define DEF_AUBIO_BUFSIZE		512
//#define DEF_AUBIO_HOPSIZE		512
//#define DEF_AUBIO_BUFSIZE		1024
#define DEF_AUBIO_SAMPLERATE	0		// read samplerate from the file
#define DEF_AUBIO_FILTER		40
#define DEF_AUBIO_COEFS			2

#define DEF_SEARCH_TOLERANCE		0.001

#define DEF_UUID_STR_LEN 37

db_ctx_t* g_db_ctx;	// database context

static bool init_database(void);

static int create_audio_list_info(const char* context, const char* filename, const char* uuid);
static bool create_audio_fingerprint_info(const char* context, const char* filename, const char* uuid);
static struct ast_json* create_audio_fingerprints(const char* filename, const char* uuid);

static struct ast_json* get_audio_list_info(const char* uuid);
static struct ast_json* get_audio_list_info_by_context_and_hash(const char* context, const char* hash);
static char* create_file_hash(const char* filename);

static bool create_context_list_info(const char* name, const char* directory, const bool replace);
static bool delete_context_list_info(const char* name);

static bool create_temp_search_table(const char* tablename);
static bool delete_temp_search_table(const char* tablename);

static char* replace_string_char(const char* str, const char org, const char target);

static db_ctx_t* create_db_ctx(void);
static void destroy_db_ctx(db_ctx_t* db_ctx);

bool fp_init(void)
{
	int ret;
	db_ctx_t* db_ctx;

	/* initiate database */
	ret = init_database();
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not initiate database.\n");
		return false;
	}

	// load the saved data into memory
	db_ctx = create_db_ctx();
	ret = db_ctx_load_db_data(db_ctx, DEF_BACKUP_DATABASE);
	destroy_db_ctx(db_ctx);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not load the database data.\n");
		return false;
	}

	return true;
}

bool fp_term(void)
{
	int ret;
	db_ctx_t* db_ctx;

	db_ctx = create_db_ctx();
	ret = db_ctx_backup(db_ctx, DEF_BACKUP_DATABASE);
	destroy_db_ctx(db_ctx);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not write database.\n");
		return false;
	}

	db_ctx_term(g_db_ctx);

	return true;
}

/**
 * Delete audio info and related fingerprint info.
 * @param uuid
 * @return
 */
bool fp_delete_audio_list_info(const char* uuid)
{
	int ret;
	struct ast_json* j_tmp;
	char* sql;
	db_ctx_t* db_ctx;

	if(uuid == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	// get audio list info
	j_tmp = get_audio_list_info(uuid);
	if(j_tmp == NULL) {
		ast_log(LOG_NOTICE, "Could not find audio list info.\n");
		return false;
	}

	// delete audio list info
	ast_asprintf(&sql, "delete from audio_list where uuid='%s';", uuid);
	db_ctx = create_db_ctx();
	ret = db_ctx_exec(db_ctx, sql);
	destroy_db_ctx(db_ctx);
	sfree(sql);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not delete audio list info. uuid[%s]\n", uuid);
		ast_json_unref(j_tmp);
		return false;
	}

	// delete related audio fingerprint info
	ast_asprintf(&sql, "delete from audio_fingerprint where audio_uuid='%s';", uuid);
	db_ctx = create_db_ctx();
	ret = db_ctx_exec(db_ctx, sql);
	destroy_db_ctx(db_ctx);
	sfree(sql);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not delete audio fingerprint info. audio_uuid[%s]\n", uuid);
		ast_json_unref(j_tmp);
		return false;
	}

	return true;
}

bool fp_craete_audio_list_info(const char* context, const char* filename)
{
	int ret;
	char* uuid;

	if((context == NULL) || (filename == NULL)) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	// create uuid
	uuid = fp_generate_uuid();

	// create audio list info
	ret = create_audio_list_info(context, filename, uuid);
	if(ret < 0) {
		ast_log(LOG_WARNING, "Could not create audio_list info. context[%s], filename[%s]\n", context, filename);
		sfree(uuid);
		return false;
	}
	else if(ret == 0) {
		ast_log(LOG_VERBOSE, "The given audio file is already exist in the list. context[%s], filename[%s]\n", context, filename);
		sfree(uuid);
		return true;
	}

	// create audio fingerprint info
	ret = create_audio_fingerprint_info(context, filename, uuid);
	sfree(uuid);
	if(ret == false) {
		ast_log(LOG_NOTICE, "Could not create audio fingerprint info.\n");
		fp_delete_audio_list_info(filename);
		return false;
	}

	return true;
}

/**
 * Search fingerprint info of given file.
 * @param context
 * @param filename
 * @param coefs
 * @param tolerance
 * @return
 */
struct ast_json* fp_search_fingerprint_info(
		const char* context,
		const char* filename,
		const int coefs,
		const double tolerance,
		const int freq_ignore_low,
		const int freq_ignore_high
		)
{
	int ret;
	char* uuid;
	char* sql;
	char* tmp;
	char* tmp_max;
	char* tablename;
	struct ast_json* j_fprints;
	struct ast_json* j_tmp;
	struct ast_json* j_search;
	struct ast_json* j_res;
	int frame_count;
	int i;
	int j;
	db_ctx_t* db_ctx;
	double tole;
	double freq;
	double freq_tmp;

	if((context == NULL) || (filename == NULL)) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}
	ast_log(LOG_DEBUG, "Fired fp_search_fingerprint_info. context[%s], filename[%s], coefs[%d], tolerance[%f], freq_ignore_low[%d], freq_ignore_high[%d]\n",
			context,
			filename,
			coefs,
			tolerance,
			freq_ignore_low,
			freq_ignore_high
			);

	if((coefs < 1) || (coefs > DEF_AUBIO_COEFS)) {
		ast_log(LOG_WARNING, "Wrong coefs count. max[%d], coefs[%d]\n", DEF_AUBIO_COEFS, coefs);
		return NULL;
	}
	
	tole = tolerance;
	if(tole < 0) {
		ast_log(LOG_NOTICE, "Wrong tolerance setting. Set to default. tolerance[%f], default[%f]\n", tolerance, DEF_SEARCH_TOLERANCE);
		tole = DEF_SEARCH_TOLERANCE;
	}

	uuid = fp_generate_uuid();

	// create tablename
	tmp = replace_string_char(uuid, '-', '_');
	ast_asprintf(&tablename, "temp_%s", tmp);
	sfree(tmp);

	// create tmp search table
	ret = create_temp_search_table(tablename);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not create temp search table. tablename[%s]\n", tablename);
		sfree(tablename);
		sfree(uuid);
		return NULL;
	}

	// create fingerprint info
	j_fprints = create_audio_fingerprints(filename, uuid);
	sfree(uuid);
	if(j_fprints == NULL) {
		ast_log(LOG_ERROR, "Could not create fingerprint info.\n");
		delete_temp_search_table(tablename);
		sfree(tablename);
		return NULL;
	}
	ast_log(LOG_DEBUG, "Created search info.\n");

	// search
	frame_count = ast_json_array_size(j_fprints);
	for(i = 0; i < frame_count; i++) {
		j_tmp = ast_json_array_get(j_fprints, i);

		freq = (int)ast_json_real_get(ast_json_object_get(j_tmp, "max1"));

		/* validate frequency range */
		if(freq_ignore_low > 0) {
			freq_tmp = 10 * log10(freq_ignore_low);
			if(freq < freq_tmp) {
				/* ignore. frequency is too low */
				continue;
			}
		}
		if(freq_ignore_high > 0) {
			freq_tmp = 10 * log10(freq_ignore_high);
			if(freq > freq_tmp) {
				/* ignore. frequency is too high */
				continue;
			}
		}

		ast_asprintf(&sql, "insert into %s select * from audio_fingerprint where "
				" max1 >= %f "
				" and max1 <= %f ",
				tablename,
				freq - tole,
				freq + tole
				);


		// add more conditions if the more coefs has given.
		for(j = 1; j < coefs; j++) {
			ast_asprintf(&tmp_max, "max%d", j + 1);

			freq = ast_json_real_get(ast_json_object_get(j_tmp, tmp_max));

			/* validate frequency range */
			if(freq_ignore_low > 0) {
				freq_tmp = 10 * log10(freq_ignore_low);
				if(freq < freq_tmp) {
					/* ignore. frequency is too low */
					continue;
				}
			}
			if(freq_ignore_high > 0) {
				freq_tmp = 10 * log10(freq_ignore_high);
				if(freq > freq_tmp) {
					/* ignore. frequency is too high */
					continue;
				}
			}

			ast_asprintf(&tmp, "%s and %s >= %f and %s <= %f",
					sql,

					tmp_max,
					freq - tole,

					tmp_max,
					freq + tole
					);
			sfree(tmp_max);
			sfree(sql);
			sql = tmp;
		}

		ast_asprintf(&tmp, "%s group by audio_uuid", sql);
		sfree(sql);
		sql = tmp;

		db_ctx = create_db_ctx();
		db_ctx_exec(db_ctx, sql);
		destroy_db_ctx(db_ctx);

		sfree(sql);
	}
	ast_json_unref(j_fprints);
	ast_log(LOG_DEBUG, "Inserted search info.\n");

	// get result
	ast_asprintf(&sql, "select *, count(*) from %s group by audio_uuid order by count(*) DESC", tablename);
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_search = db_ctx_get_record(db_ctx);
	destroy_db_ctx(db_ctx);
	ast_log(LOG_DEBUG, "Executed query.\n");

	// delete temp search table
	ret = delete_temp_search_table(tablename);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not delete temp search table. tablename[%s]\n", tablename);
		sfree(tablename);
		ast_json_unref(j_search);
		return false;
	}
	sfree(tablename);

	if(j_search == NULL) {
		// not found
		ast_log(LOG_NOTICE, "Could not find data.\n");
		return NULL;
	}
	ast_log(LOG_DEBUG, "Search complete.\n");

	// create result
	j_res = get_audio_list_info(ast_json_string_get(ast_json_object_get(j_search, "audio_uuid")));
	if(j_res == NULL) {
		ast_log(LOG_WARNING, "Could not find audio list info.\n");
		ast_json_unref(j_search);
		return NULL;
	}
	ast_log(LOG_DEBUG, "Created result.\n");


	ast_json_object_set(j_res, "frame_count", ast_json_integer_create(frame_count));
	ast_json_object_set(j_res, "match_count", ast_json_ref(ast_json_object_get(j_search, "count(*)")));
	ast_json_unref(j_search);

	return j_res;
}

/**
 * Returns all list of fingerprinted info.
 * @return
 */
struct ast_json* fp_get_audio_lists_all(void)
{
	char* sql;
	struct ast_json* j_res;
	struct ast_json* j_tmp;
	db_ctx_t* db_ctx;

	// get result
	ast_asprintf(&sql, "%s", "select * from audio_list;");
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_res = ast_json_array_create();
	while(1) {
		j_tmp = db_ctx_get_record(db_ctx);
		if(j_tmp == NULL) {
			break;
		}

		ast_json_array_append(j_res, j_tmp);
	}
	destroy_db_ctx(db_ctx);

	return j_res;
}

struct ast_json* fp_get_audio_lists_by_contextname(const char* name)
{
	struct ast_json* j_res;
	struct ast_json* j_tmp;
	char* sql;
	db_ctx_t* db_ctx;

	if(name == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}

	ast_asprintf(&sql, "select * from audio_list where context = '%s';", name);
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_res = ast_json_array_create();
	while(1) {
		j_tmp = db_ctx_get_record(db_ctx);
		if(j_tmp == NULL) {
			break;
		}

		ast_json_array_append(j_res, j_tmp);
	}
	destroy_db_ctx(db_ctx);

	return j_res;
}

/**
 * Create audio list data and insert it.
 * If the file is already listed, return false.
 * @param filename
 * @param uuid
 * @return 1:created audio_list info correctly, 0:already exist, -1:error occurred
 */
static int create_audio_list_info(const char* context, const char* filename, const char* uuid)
{
	int ret;
	char* hash;
	char* tmp;
	const char* name;
	struct ast_json* j_tmp;

	if((context == NULL) || (filename == NULL) || (uuid == NULL)) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return -1;
	}
	ast_log(LOG_DEBUG, "Fired create_audio_list_info. context[%s], filename[%s], uuid[%s]\n", context, filename, uuid);

	// create file hash
	hash = create_file_hash(filename);
	if(hash == NULL) {
		ast_log(LOG_WARNING, "Could not create hash info.\n");
		return -1;
	}
	ast_log(LOG_DEBUG, "Created hash. hash[%s]\n", hash);

	// check existence
	j_tmp = get_audio_list_info_by_context_and_hash(context, hash);
	if(j_tmp != NULL) {
		ast_log(LOG_VERBOSE, "The given file is already fingerprinted. context[%s], filename[%s]\n", context, filename);
		sfree(hash);
		return 0;
	}

	// craete data
	tmp = ast_strdup(filename);
	name = basename(tmp);
	j_tmp = ast_json_pack("{s:s, s:s, s:s, s:s}",
			"uuid", 	uuid,
			"name",		name,
			"context",	context,
			"hash",		hash
			);
	sfree(tmp);
	sfree(hash);

	// insert
	ret = db_ctx_insert(g_db_ctx, "audio_list", j_tmp);
	ast_json_unref(j_tmp);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not create fingerprint info.\n");
		return -1;
	}

	return 1;
}

/**
 * Create audio fingerprint data and insert it.
 * @param filename
 * @param uuid
 * @return
 */
static bool create_audio_fingerprint_info(const char* context, const char* filename, const char* uuid)
{
	int ret;
	int idx;
	struct ast_json* j_fprint;
	struct ast_json* j_fprints;

	if((filename == NULL) || (uuid == NULL)) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}
	ast_log(LOG_DEBUG, "Fired create_audio_fingerprint_info. filename[%s], uuid[%s]\n", filename, uuid);

	// craete fingerprint data
	j_fprints = create_audio_fingerprints(filename, uuid);
	if(j_fprints == NULL) {
		ast_log(LOG_ERROR, "Could not create fingerprint data.\n");
		return false;
	}

	// insert data
	for(idx = 0; idx < ast_json_array_size(j_fprints); idx++) {
		j_fprint = ast_json_array_get(j_fprints, idx);
		if(j_fprint == NULL) {
			continue;
		}

		ast_json_object_set(j_fprint, "context", ast_json_string_create(context));
		ret = db_ctx_insert(g_db_ctx, "audio_fingerprint", j_fprint);
		if(ret == false) {
			ast_log(LOG_WARNING, "Could not insert fingerprint data.\n");
			continue;
		}
	}
	ast_json_unref(j_fprints);

	return true;
}

static struct ast_json* create_audio_fingerprints(const char* filename, const char* uuid)
{
	struct ast_json* j_res;
	struct ast_json* j_tmp;
	unsigned int reads;
	int count;
	int samplerate;
	char* source;
	int i;
	char col_max[10];

	aubio_pvoc_t* pv;
	cvec_t*	fftgrain;
	aubio_mfcc_t* mfcc;
	fvec_t* mfcc_out;
	fvec_t* mfcc_buf;

	aubio_source_t* aubio_src;

	if((filename == NULL) || (uuid == NULL)) {
		fprintf(stderr, "Wrong input parameter.\n");
		return NULL;
	}
	ast_log(LOG_DEBUG, "Fired create_audio_fingerprints. filename[%s], uuid[%s]\n", filename, uuid);

	// initiate aubio src
	source = ast_strdup(filename);
	aubio_src = new_aubio_source(source, DEF_AUBIO_SAMPLERATE, DEF_AUBIO_HOPSIZE);
	sfree(source);
	if(aubio_src == NULL) {
		ast_log(LOG_ERROR, "Could not initiate aubio src.\n");
		return NULL;
	}

	// initiate aubio parameters
	samplerate = aubio_source_get_samplerate(aubio_src);
	pv = new_aubio_pvoc(DEF_AUBIO_BUFSIZE, DEF_AUBIO_HOPSIZE);
	fftgrain = new_cvec(DEF_AUBIO_BUFSIZE);
	mfcc = new_aubio_mfcc(DEF_AUBIO_BUFSIZE, DEF_AUBIO_FILTER, DEF_AUBIO_COEFS, samplerate);
	mfcc_buf = new_fvec(DEF_AUBIO_HOPSIZE);
	mfcc_out = new_fvec(DEF_AUBIO_COEFS);
	if((pv == NULL) || (fftgrain == NULL) || (mfcc == NULL) || (mfcc_buf == NULL) || (mfcc_out == NULL)) {
		ast_log(LOG_ERROR, "Could not initiate aubio parameters.\n");

		del_aubio_pvoc(pv);
		del_cvec(fftgrain);
		del_aubio_mfcc(mfcc);
		del_fvec(mfcc_out);
		del_fvec(mfcc_buf);
		del_aubio_source(aubio_src);
		return NULL;
	}

	j_res = ast_json_array_create();
	count = 0;
	while(1) {
		aubio_source_do(aubio_src, mfcc_buf, &reads);
		if(reads == 0) {
		  break;
		}

		// compute mag spectrum
		aubio_pvoc_do(pv, mfcc_buf, fftgrain);

		// compute mfcc
		aubio_mfcc_do(mfcc, fftgrain, mfcc_out);

		// create mfcc data
		j_tmp = ast_json_pack("{s:i, s:s}",
				"frame_idx",	count,
				"audio_uuid",	uuid
				);
		for(i = 0; i < DEF_AUBIO_COEFS; i++) {
			snprintf(col_max, sizeof(col_max), "max%d", i + 1);
			ast_json_object_set(j_tmp, col_max, ast_json_real_create(10 * log10(fabs(mfcc_out->data[i]))));
		}

		if(j_tmp == NULL) {
			ast_log(LOG_ERROR, "Could not create mfcc data.\n");
			continue;
		}

		ast_json_array_append(j_res, j_tmp);
		count++;
	}

	del_aubio_pvoc(pv);
	del_cvec(fftgrain);
	del_aubio_mfcc(mfcc);
	del_fvec(mfcc_out);
	del_fvec(mfcc_buf);
	del_aubio_source(aubio_src);

	return j_res;
}

static bool init_database(void)
{
	int ret;
	char* sql;
	char* tmp;
	int i;

	g_db_ctx = db_ctx_init(DEF_DATABASE_NAME);
	if(g_db_ctx == NULL) {
		return false;
	}

	/* context_list */
	sql = "create table context_list("

			"   name        varchar(255),"
			"   directory   varchar(1023),"

			"   primary key(name)"
			");";
	ret = db_ctx_exec(g_db_ctx, sql);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not create context_list table.\n");
		return false;
	}

	/* audio_list */
	sql = "create table audio_list("

			"   uuid           varchar(255),"
			"   name           varchar(255),"
			"   context        varchar(255),"	// context name
			"	hash           varchar(1023)"
			");";
	ret = db_ctx_exec(g_db_ctx, sql);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not create auido_list table.\n");
		return false;
	}

	/* audio_fingerprint */
	ast_asprintf(&sql, "%s",
			"create table audio_fingerprint("

			" context        varchar(255),"
			" audio_uuid     varchar(255),"
			" frame_idx      integer");
	for(i = 0; i < DEF_AUBIO_COEFS; i++) {
		ast_asprintf(&tmp, "%s, max%d real", sql, i + 1);
		sfree(sql);
		sql = tmp;
	}
	ast_asprintf(&tmp, "%s);", sql);
	sfree(sql);
	sql = tmp;
	ret = db_ctx_exec(g_db_ctx, sql);
	sfree(sql);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not create fingerprint table.\n");
		return false;
	}

	// create index for context
	ast_asprintf(&sql, "%s", "create index idx_audio_fingerprint_context on audio_fingerprint(context);");
	ret = db_ctx_exec(g_db_ctx, sql);
	sfree(sql);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not create idx_audio_fingerprint_max%d table.\n", i);
		return false;
	}

	// create indices for max
	for(i = 1; i <= DEF_AUBIO_COEFS; i++) {
		ast_asprintf(&sql, "create index idx_audio_fingerprint_max%d on audio_fingerprint(max%d);", i, i);
		ret = db_ctx_exec(g_db_ctx, sql);
		sfree(sql);
		if(ret == false) {
			ast_log(LOG_ERROR, "Could not create idx_audio_fingerprint_max%d table.\n", i);
			return false;
		}
	}

	return true;
}

static char* create_file_hash(const char* filename)
{
	unsigned char hash[MD5_DIGEST_LENGTH];
	int i;
	FILE* file;
	MD5_CTX md_ctx;
	int read;
	unsigned char data[1024];
	char* res;
	char* tmp;

	if(filename == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}

	file = fopen (filename, "rb");
	if(file == NULL) {
		ast_log(LOG_WARNING, "Could not open file. filename[%s]\n", filename);
		return NULL;
	}

	// create hash info
	MD5_Init (&md_ctx);
	while(1) {
		read = fread(data, 1, sizeof(data), file);
		if(read == 0) {
			break;
		}
		MD5_Update(&md_ctx, data, read);
	}
	fclose(file);

	// make hash
	MD5_Final(hash, &md_ctx);

	// create result
	tmp = NULL;
	res = NULL;
    for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
    	ast_asprintf(&tmp, "%s%02x", res? : "", hash[i]);
    	sfree(res);
    	res = ast_strdup(tmp);
    	sfree(tmp);
    }

	return res;
}

static struct ast_json* get_audio_list_info_by_context_and_hash(const char* context, const char* hash)
{
	char* sql;
	struct ast_json* j_res;
	db_ctx_t* db_ctx;

	if((context == NULL) || (hash == NULL)) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}

	ast_asprintf(&sql, "select * from audio_list where context = '%s' and hash = '%s';", context, hash);
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_res = db_ctx_get_record(db_ctx);
	destroy_db_ctx(db_ctx);
	if(j_res == NULL) {
		return NULL;
	}

	return j_res;
}

static struct ast_json* get_audio_list_info(const char* uuid)
{
	char* sql;
	struct ast_json* j_res;
	db_ctx_t* db_ctx;

	if(uuid == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}

	ast_asprintf(&sql, "select * from audio_list where uuid = '%s';", uuid);
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_res = db_ctx_get_record(db_ctx);
	destroy_db_ctx(db_ctx);
	if(j_res == NULL) {
		return NULL;
	}

	return j_res;
}

static bool create_temp_search_table(const char* tablename)
{
	char* sql;
	char* tmp;
	int i;
	int ret;

	if(tablename == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	// create audio_fingerprint table
	ast_asprintf(&sql, "create table %s("

			" context        varchar(255),"
			" audio_uuid     varchar(255),"
			" frame_idx      integer",
			tablename
			);
	for(i = 0; i < DEF_AUBIO_COEFS; i++) {
		ast_asprintf(&tmp, "%s, max%d real", sql, i + 1);
		sfree(sql);
		sql = tmp;
	}
	ast_asprintf(&tmp, "%s);", sql);
	sfree(sql);
	sql = tmp;
	ret = db_ctx_exec(g_db_ctx, sql);
	sfree(sql);
	if(ret == false) {
		ast_log(LOG_ERROR, "Could not create fingerprint search table.\n");
		return false;
	}

	return true;
}

static bool delete_temp_search_table(const char* tablename)
{
	char* sql;

	if(tablename == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	ast_asprintf(&sql, "drop table %s;", tablename);

	db_ctx_exec(g_db_ctx, sql);
	sfree(sql);

	return true;
}

struct ast_json* fp_get_context_lists_all(void)
{
	char* sql;
	struct ast_json* j_res;
	struct ast_json* j_tmp;
	db_ctx_t* db_ctx;

	ast_asprintf(&sql, "%s", "select * from context_list;");
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_res = ast_json_array_create();
	while(1) {
		j_tmp = db_ctx_get_record(db_ctx);
		if(j_tmp == NULL) {
			break;
		}

		ast_json_array_append(j_res, j_tmp);
	}
	destroy_db_ctx(db_ctx);

	return j_res;
}

struct ast_json* fp_get_context_list_info(const char* name)
{
	char* sql;
	struct ast_json* j_res;
	db_ctx_t* db_ctx;

	if(name == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}

	ast_asprintf(&sql, "select * from context_list where name == '%s';", name);
	db_ctx = create_db_ctx();
	db_ctx_query(db_ctx, sql);
	sfree(sql);

	j_res = db_ctx_get_record(db_ctx);
	destroy_db_ctx(db_ctx);
	if(j_res == NULL) {
		return NULL;
	}

	return j_res;
}


static bool create_context_list_info(const char* name, const char* directory, const bool replace)
{
	int ret;
	struct ast_json* j_data;

	if(name == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	j_data = ast_json_pack("{s:s, s:s}",
			"name",			name,
			"directory",	directory
			);

	if(replace == false) {
		ret = db_ctx_insert(g_db_ctx, "context_list", j_data);
	}
	else {
		ret = db_ctx_insert_or_replace(g_db_ctx, "context_list", j_data);
	}
	ast_json_unref(j_data);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not insert data into database.\n");
		return false;
	}

	return true;
}

static bool delete_context_list_info(const char* name)
{
	int ret;
	char* sql;

	if(name == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	ast_asprintf(&sql, "delete from context_list where name == '%s';", name);

	ret = db_ctx_exec(g_db_ctx, sql);
	sfree(sql);
	if(ret == false) {
		ast_log(LOG_NOTICE, "Could not delete context_list info. name[%s]\n", name);
		return false;
	}

	return true;
}

/**
 * Create context_list info.
 * @param name
 * @return
 */
bool fp_create_context_list_info(const char* name, const char* directory, bool replace)
{
	int ret;

	ret = create_context_list_info(name, directory, replace);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not create context list info. name[%s]\n", name);
		return false;
	}

	return true;
}

/**
 * Delete context_list info with all related info.
 * @param name
 * @return
 */
bool fp_delete_context_list_info(const char* name)
{
	int ret;
	int idx;
	struct ast_json* j_context;
	struct ast_json* j_audio_lists;
	struct ast_json* j_audio_list;
	const char* uuid;

	if(name == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return false;
	}

	j_context = fp_get_context_list_info(name);
	if(j_context == NULL) {
		ast_log(LOG_NOTICE, "Could not find context info. context[%s]\n", name);
		return false;
	}

	/* get all audio_list info of the given context */
	j_audio_lists = fp_get_audio_lists_by_contextname(name);
	if(j_audio_lists == NULL) {
		ast_log(LOG_WARNING, "Could not get audio_list info. context[%s]\n", name);
		return false;
	}

	/* delete all audio_list info of belongings */
	for(idx = 0; idx < ast_json_array_size(j_audio_lists); idx++) {
		j_audio_list = ast_json_array_get(j_audio_lists, idx);
		if(j_audio_list == NULL) {
			continue;
		}

		uuid = ast_json_string_get(ast_json_object_get(j_audio_list, "uuid"));
		if(uuid == NULL) {
			continue;
		}

		/* delete audio_list */
		ret = fp_delete_audio_list_info(uuid);
		if(ret != true) {
			ast_log(LOG_WARNING, "Could not delete audio_list info. uuid[%s]\n", uuid);
			continue;
		}
	}
	ast_json_unref(j_audio_lists);

	/* delete context */
	ret = delete_context_list_info(name);
	if(ret == false) {
		ast_log(LOG_WARNING, "Could not delete context list info.\n");
		return false;
	}

	return true;
}

char* fp_generate_uuid(void)
{
	uuid_t uuid;
	char tmp[DEF_UUID_STR_LEN];
	char* res;

	uuid_generate(uuid);
	uuid_unparse_lower(uuid, tmp);

	res = ast_strdup(tmp);

	return res;
}

char* fp_create_hash(const char* filename)
{
	char* res;

	if(filename == NULL) {
		ast_log(LOG_WARNING, "Wrong input parameter.\n");
		return NULL;
	}

	res = create_file_hash(filename);
	if(res == NULL) {
		return NULL;
	}

	return res;
}

/**
 * Copy the given str and replace given org character to target character from str.
 * Return string should be freed after use it.
 * @param uuid
 * @return
 */
static char* replace_string_char(const char* str, const char org, const char target)
{
	char* tmp;
	int len;
	int i;
	int j;

	if(str == NULL) {
		return NULL;
	}

	len = strlen(str);
	tmp = ast_calloc(len + 1, sizeof(char));
	j = 0;
	for(i = 0; i < len; i++) {
		if(str[i] == org) {
			tmp[j] = target;
		}
		else {
			tmp[j] = str[i];
		}
		j++;
	}
	tmp[j] = '\0';
	return tmp;
}

static db_ctx_t* create_db_ctx(void)
{
	db_ctx_t* db_ctx;

	db_ctx = ast_calloc(1, sizeof(db_ctx_t));
	db_ctx->db = g_db_ctx->db;

	return db_ctx;
}

static void destroy_db_ctx(db_ctx_t* db_ctx)
{
	if(db_ctx == NULL) {
		return;
	}

	db_ctx_free(db_ctx);
	sfree(db_ctx);

	return;
}

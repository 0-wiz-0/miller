#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "lib/mlr_globals.h"
#include "lib/mlrutil.h"
#include "lib/string_builder.h"
#include "input/file_reader_stdio.h"
#include "input/byte_reader.h"
#include "input/lrec_readers.h"
#include "input/peek_file_reader.h"
#include "containers/slls.h"
#include "containers/lhmslv.h"
#include "containers/parse_trie.h"

// Idea of pheader_keepers: each header_keeper object retains the input-line backing
// and the slls_t for a CSV header line which is used by one or more CSV data
// lines.  Meanwhile some mappers retain input records from the entire data
// stream, including header-schema changes in the input stream. This means we
// need to keep headers intact as long as any lrecs are pointing to them.  One
// option is reference-counting which I experimented with; it was messy and
// error-prone. The approach used here is to keep a hash map from header-schema
// to header_keeper object. The current pheader_keeper is a pointer into one of
// those.  Then when the reader is freed, all the header-keepers are freed.

// ----------------------------------------------------------------
#define STRING_BUILDER_INIT_SIZE 1024

// xxx globally rename stridx to token/label
#define EOF_STRIDX           0x2000
#define IRS_STRIDX           0x2001
#define IFS_EOF_STRIDX       0x2002
#define IFS_STRIDX           0x2003
#define DQUOTE_STRIDX        0x2004
#define DQUOTE_IRS_STRIDX    0x2005
#define DQUOTE_IFS_STRIDX    0x2006
#define DQUOTE_EOF_STRIDX    0x2007
#define DQUOTE_DQUOTE_STRIDX 0x2008

// ----------------------------------------------------------------
typedef struct _lrec_reader_csv_state_t {
	// Input line number is not the same as the record-counter in context_t,
	// which counts records.
	long long  ilno;

	char* eof;
	char* irs;
	char* ifs_eof;
	char* ifs;
	char* dquote;
	char* dquote_irs;
	char* dquote_ifs;
	char* dquote_eof;
	char* dquote_dquote;

	int   eof_len;
	int   irs_len;
	int   ifs_eof_len;
	int   ifs_len;
	int   dquote_len;
	int   dquote_irs_len;
	int   dquote_ifs_len;
	int   dquote_eof_len;
	int   dquote_dquote_len;

	int   peek_buf_len;

	string_builder_t    sb;
	string_builder_t*   psb;
	byte_reader_t*      pbr;
	peek_file_reader_t* pfr;

	parse_trie_t*       pno_dquote_parse_trie;
	parse_trie_t*       pdquote_parse_trie;

	int                 expect_header_line_next;
	header_keeper_t*    pheader_keeper;
	lhmslv_t*           pheader_keepers;

} lrec_reader_csv_state_t;

static slls_t* lrec_reader_csv_get_fields(lrec_reader_csv_state_t* pstate);
static lrec_t* paste_header_and_data(lrec_reader_csv_state_t* pstate, slls_t* pdata_fields);

// ----------------------------------------------------------------
// xxx needs abend on null lhs. etc.

static lrec_t* lrec_reader_csv_process(void* pvstate, void* pvhandle, context_t* pctx) {
	lrec_reader_csv_state_t* pstate = pvstate;

	if (pstate->expect_header_line_next) {
		slls_t* pheader_fields = lrec_reader_csv_get_fields(pstate);
		if (pheader_fields == NULL)
			return NULL;
		pstate->ilno++;
		pstate->expect_header_line_next = FALSE;

		pstate->pheader_keeper = lhmslv_get(pstate->pheader_keepers, pheader_fields);
		if (pstate->pheader_keeper == NULL) {
			pstate->pheader_keeper = header_keeper_alloc(NULL, pheader_fields);
			lhmslv_put(pstate->pheader_keepers, pheader_fields, pstate->pheader_keeper);
		} else { // Re-use the header-keeper in the header cache
			slls_free(pheader_fields);
		}
	}
	pstate->ilno++;

	slls_t* pdata_fields = lrec_reader_csv_get_fields(pstate);
	if (pdata_fields == NULL) // EOF
		return NULL;
	else {
		//lrec_t* prec = paste_header_and_data(pstate, pdata_fields);
		//slls_free(pdata_fields);
		//return prec;
		return paste_header_and_data(pstate, pdata_fields);
	}
}

static slls_t* lrec_reader_csv_get_fields(lrec_reader_csv_state_t* pstate) {
	int rc, stridx, matchlen, record_done, field_done;
	peek_file_reader_t* pfr = pstate->pfr;
	string_builder_t*   psb = pstate->psb;

	if (pfr_peek_char(pfr) == EOF)
		return NULL;
	slls_t* pfields = slls_alloc();

	// loop over fields in record
	record_done = FALSE;
	while (!record_done) {
		// xxx fix me char vs. string ...
		if (pfr_peek_char(pfr) != pstate->dquote[0]) {

			// loop over characters in field
			field_done = FALSE;
			while (!field_done) {
				pfr_buffer_by(pfr, pstate->pno_dquote_parse_trie->maxlen);

				rc = parse_trie_match(pstate->pno_dquote_parse_trie,
					pfr->peekbuf, pfr->sob, pfr->npeeked, pfr->peekbuflenmask,
					&stridx, &matchlen);
#ifdef DEBUG_PARSER
				pfr_print(pfr);
#endif
				if (rc) {
#ifdef DEBUG_PARSER
					printf("RC=%d stridx=0x%04x matchlen=%d\n", rc, stridx, matchlen);
#endif
					switch(stridx) {
					case EOF_STRIDX: // end of record
						slls_add_with_free(pfields, sb_finish(psb));
						field_done  = TRUE;
						record_done = TRUE;
						break;
					case IFS_EOF_STRIDX:
						fprintf(stderr, "%s: syntax error: record-ending field separator at line %lld.\n",
							MLR_GLOBALS.argv0, pstate->ilno);
						exit(1);
						break;
					case IFS_STRIDX: // end of field
						slls_add_with_free(pfields, sb_finish(psb));
						field_done  = TRUE;
						break;
					case IRS_STRIDX: // end of record
						slls_add_with_free(pfields, sb_finish(psb));
						field_done  = TRUE;
						record_done = TRUE;
						break;
					case DQUOTE_STRIDX: // CSV syntax error: fields containing quotes must be fully wrapped in quotes
						fprintf(stderr, "%s: syntax error: unwrapped double quote at line %lld.\n",
							MLR_GLOBALS.argv0, pstate->ilno);
						exit(1);
						break;
					default:
						fprintf(stderr, "%s: internal coding error: unexpected token %d at line %lld.\n",
							MLR_GLOBALS.argv0, stridx, pstate->ilno);
						exit(1);
						break;
					}
					pfr_advance_by(pfr, matchlen);
				} else {
#ifdef DEBUG_PARSER
					char c = pfr_read_char(pfr);
					printf("CHAR=%c [%02x]\n", isprint(c) ? c : ' ', (unsigned)c);
					sb_append_char(psb, c);
#else
					sb_append_char(psb, pfr_read_char(pfr));
#endif
				}
			}

		} else {
			pfr_advance_by(pfr, pstate->dquote_len);

			// loop over characters in field
			field_done = FALSE;
			while (!field_done) {
				pfr_buffer_by(pfr, pstate->pdquote_parse_trie->maxlen);

				rc = parse_trie_match(pstate->pdquote_parse_trie,
					pfr->peekbuf, pfr->sob, pfr->npeeked, pfr->peekbuflenmask,
					&stridx, &matchlen);

				if (rc) {
					switch(stridx) {
					case EOF_STRIDX: // end of record
						fprintf(stderr, "%s: imbalanced double-quote at line %lld.\n",
							MLR_GLOBALS.argv0, pstate->ilno);
						exit(1);
						break;
					case DQUOTE_EOF_STRIDX: // end of record
						slls_add_with_free(pfields, sb_finish(psb));
						field_done  = TRUE;
						record_done = TRUE;
						break;
					case DQUOTE_IFS_STRIDX: // end of field
						slls_add_with_free(pfields, sb_finish(psb));
						field_done  = TRUE;
						break;
					case DQUOTE_IRS_STRIDX: // end of record
						slls_add_with_free(pfields, sb_finish(psb));
						field_done  = TRUE;
						record_done = TRUE;
						break;
					case DQUOTE_DQUOTE_STRIDX: // RFC-4180 CSV: "" inside a dquoted field is an escape for "
						sb_append_char(psb, pstate->dquote[0]);
						break;
					default:
						fprintf(stderr, "%s: internal coding error: unexpected token %d at line %lld.\n",
							MLR_GLOBALS.argv0, stridx, pstate->ilno);
						exit(1);
						break;
					}
					pfr_advance_by(pfr, matchlen);
				} else {
					sb_append_char(psb, pfr_read_char(pfr));
				}
			}

		}
	}

	return pfields;
}

// ----------------------------------------------------------------
static lrec_t* paste_header_and_data(lrec_reader_csv_state_t* pstate, slls_t* pdata_fields) {
	if (pstate->pheader_keeper->pkeys->length != pdata_fields->length) {
		fprintf(stderr, "%s: Header/data length mismatch: %d != %d at line %lld.\n",
			MLR_GLOBALS.argv0, pstate->pheader_keeper->pkeys->length, pdata_fields->length, pstate->ilno);
		exit(1);
	}
	lrec_t* prec = lrec_unbacked_alloc();
	sllse_t* ph = pstate->pheader_keeper->pkeys->phead;
	sllse_t* pd = pdata_fields->phead;
	for ( ; ph != NULL && pd != NULL; ph = ph->pnext, pd = pd->pnext) {
		lrec_put_no_free(prec, ph->value, pd->value);
		//lrec_put(prec, ph->value, pd->value, LREC_FREE_ENTRY_VALUE);
	}
	return prec;
}

// ----------------------------------------------------------------
void* lrec_reader_csv_open(void* pvstate, char* filename) {
	lrec_reader_csv_state_t* pstate = pvstate;
	pstate->pfr->pbr->popen_func(pstate->pfr->pbr, filename);
	pfr_reset(pstate->pfr);
	return NULL; // xxx modify the API after the functional refactor is complete
}

void lrec_reader_csv_close(void* pvstate, void* pvhandle) {
	lrec_reader_csv_state_t* pstate = pvstate;
	pstate->pfr->pbr->pclose_func(pstate->pfr->pbr);
}

// ----------------------------------------------------------------
// xxx after the pfr/pbr refactor is complete, vsof and vopen may be redundant.
static void lrec_reader_csv_sof(void* pvstate) {
	lrec_reader_csv_state_t* pstate = pvstate;
	pstate->ilno = 0LL;
	pstate->expect_header_line_next = TRUE;
}

// ----------------------------------------------------------------
static void lrec_reader_csv_free(void* pvstate) {
	lrec_reader_csv_state_t* pstate = pvstate;
	for (lhmslve_t* pe = pstate->pheader_keepers->phead; pe != NULL; pe = pe->pnext) {
		header_keeper_t* pheader_keeper = pe->pvvalue;
		header_keeper_free(pheader_keeper);
	}
	pfr_free(pstate->pfr);
}

// ----------------------------------------------------------------
lrec_reader_t* lrec_reader_csv_alloc(byte_reader_t* pbr, char* irs, char* ifs) {
	lrec_reader_t* plrec_reader = mlr_malloc_or_die(sizeof(lrec_reader_t));

	lrec_reader_csv_state_t* pstate = mlr_malloc_or_die(sizeof(lrec_reader_csv_state_t));
	pstate->ilno                      = 0LL;

	pstate->eof                       = "\xff";
	pstate->irs                       = irs;
	pstate->ifs                       = ifs;
	pstate->ifs_eof                   = mlr_paste_2_strings(pstate->ifs, "\xff");
	pstate->dquote                    = "\"";

	pstate->dquote_irs                = mlr_paste_2_strings("\"", pstate->irs);
	pstate->dquote_ifs                = mlr_paste_2_strings("\"", pstate->ifs);
	pstate->dquote_eof                = "\"\xff";
	pstate->dquote_dquote             = "\"\"";

	// xxx maybe not retain all these variables now that that info is in the parse-tries -- ?
	pstate->irs_len                   = strlen(pstate->irs);
	pstate->ifs_eof_len               = strlen(pstate->ifs_eof);
	pstate->ifs_len                   = strlen(pstate->ifs);
	pstate->dquote_len                = strlen(pstate->dquote);

	pstate->dquote_irs_len            = strlen(pstate->dquote_irs);
	pstate->dquote_ifs_len            = strlen(pstate->dquote_ifs);
	pstate->dquote_eof_len            = strlen(pstate->dquote_eof);
	pstate->dquote_dquote_len         = strlen(pstate->dquote_dquote);

	// xxx rid of now that this is tracked in the parse-tries -- ?
	pstate->peek_buf_len              = pstate->irs_len;
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->ifs_eof_len);
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->ifs_len);
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->dquote_len);
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->dquote_irs_len);
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->dquote_ifs_len);
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->dquote_eof_len);
	pstate->peek_buf_len              = mlr_imax2(pstate->peek_buf_len, pstate->dquote_dquote_len);
	pstate->peek_buf_len             += 2;

	pstate->psb                       = &pstate->sb;
	sb_init(pstate->psb, STRING_BUILDER_INIT_SIZE);
	pstate->pbr                       = pbr;
	pstate->pfr                       = pfr_alloc(pstate->pbr, pstate->peek_buf_len);

	pstate->pno_dquote_parse_trie = parse_trie_alloc();
	parse_trie_add_string(pstate->pno_dquote_parse_trie, pstate->eof,     EOF_STRIDX);
	parse_trie_add_string(pstate->pno_dquote_parse_trie, pstate->irs,     IRS_STRIDX);
	parse_trie_add_string(pstate->pno_dquote_parse_trie, pstate->ifs_eof, IFS_EOF_STRIDX);
	parse_trie_add_string(pstate->pno_dquote_parse_trie, pstate->ifs,     IFS_STRIDX);
	parse_trie_add_string(pstate->pno_dquote_parse_trie, pstate->dquote,  DQUOTE_STRIDX);

	pstate->pdquote_parse_trie = parse_trie_alloc();
	parse_trie_add_string(pstate->pdquote_parse_trie, pstate->eof,           EOF_STRIDX);
	parse_trie_add_string(pstate->pdquote_parse_trie, pstate->dquote_irs,    DQUOTE_IRS_STRIDX);
	parse_trie_add_string(pstate->pdquote_parse_trie, pstate->dquote_ifs,    DQUOTE_IFS_STRIDX);
	parse_trie_add_string(pstate->pdquote_parse_trie, pstate->dquote_eof,    DQUOTE_EOF_STRIDX);
	parse_trie_add_string(pstate->pdquote_parse_trie, pstate->dquote_dquote, DQUOTE_DQUOTE_STRIDX);

	pstate->expect_header_line_next   = TRUE;
	pstate->pheader_keeper            = NULL;
	pstate->pheader_keepers           = lhmslv_alloc();

	plrec_reader->pvstate       = (void*)pstate;
	plrec_reader->popen_func    = &lrec_reader_csv_open;
	plrec_reader->pclose_func   = &lrec_reader_csv_close;
	plrec_reader->pprocess_func = &lrec_reader_csv_process;
	plrec_reader->psof_func     = &lrec_reader_csv_sof;
	plrec_reader->pfree_func    = &lrec_reader_csv_free;

	return plrec_reader;
}

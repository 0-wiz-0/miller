#include <stdio.h>
#include <stdlib.h>
#include "lib/mlrutil.h"
#include "containers/slls.h"
#include "containers/lhmslv.h"
#include "input/file_reader_stdio.h"
#include "input/line_readers.h"
#include "input/lrec_readers.h"

// Idea of pheader_keepers: each header_keeper object retains the input-line backing
// and the slls_t for a CSV header line which is used by one or more CSV data
// lines.  Meanwhile some mappers retain input records from the entire data
// stream, including header-schema changes in the input stream. This means we
// need to keep headers intact as long as any lrecs are pointing to them.  One
// option is reference-counting which I experimented with; it was messy and
// error-prone. The approach used here is to keep a hash map from header-schema
// to header_keeper object. The current pheader_keeper is a pointer into one of
// those.  Then when the reader is freed, all the header-keepers are freed.

typedef struct _lrec_reader_stdio_csvlite_state_t {
	long long  ifnr; // xxx cmt w/r/t pctx
	long long  ilno; // xxx cmt w/r/t pctx
	char* irs;
	char* ifs;
	int   irslen;
	int   ifslen;
	int   allow_repeat_ifs;

	int  expect_header_line_next;
	header_keeper_t* pheader_keeper;
	lhmslv_t*     pheader_keepers;
} lrec_reader_stdio_csvlite_state_t;

// Cases:
//
// a,a        a,b        c          d
// -- FILE1:  -- FILE1:  -- FILE1:  -- FILE1:
// a,b,c      a,b,c      a,b,c      a,b,c
// 1,2,3      1,2,3      1,2,3      1,2,3
// 4,5,6      4,5,6      4,5,6      4,5,6
// -- FILE2:  -- FILE2:
// a,b,c      d,e,f,g    a,b,c      d,e,f
// 7,8,9      3,4,5,6    7,8,9      3,4,5
// --OUTPUT:  --OUTPUT:  --OUTPUT:  --OUTPUT:
// a,b,c      a,b,c      a,b,c      a,b,c
// 1,2,3      1,2,3      1,2,3      1,2,3
// 4,5,6      4,5,6      4,5,6      4,5,6
// 7,8,9                 7,8,9
//            d,e,f,g               d,e,f
//            3,4,5,6               3,4,5

// ----------------------------------------------------------------
// xxx needs abend on null lhs.
//
// etc.

static lrec_t* lrec_reader_stdio_csvlite_process(void* pvstate, void* pvhandle, context_t* pctx) {
	FILE* input_stream = pvhandle;
	lrec_reader_stdio_csvlite_state_t* pstate = pvstate;

	while (TRUE) {
		if (pstate->expect_header_line_next) {
			// xxx cmt
			while (TRUE) {
				// xxx uniformize api, then funcptrs ...
				char* hline = (pstate->irslen == 1)
					? mlr_get_cline(input_stream, pstate->irs[0])
					: mlr_get_sline(input_stream, pstate->irs, pstate->irslen);
				if (hline == NULL) // EOF
					return NULL;
				pstate->ilno++;

				slls_t* pheader_fields = (pstate->ifslen == 1)
					// xxx uniformize api, then funcptrs ...
					? split_csvlite_header_line_single_ifs(hline, pstate->ifs[0], pstate->allow_repeat_ifs)
					: split_csvlite_header_line_multi_ifs(hline, pstate->ifs, pstate->ifslen, pstate->allow_repeat_ifs);
				if (pheader_fields->length == 0) {
					pstate->expect_header_line_next = TRUE;
					if (pstate->pheader_keeper != NULL) {
						pstate->pheader_keeper = NULL;
					}
				} else {
					pstate->expect_header_line_next = FALSE;

					pstate->pheader_keeper = lhmslv_get(pstate->pheader_keepers, pheader_fields);
					if (pstate->pheader_keeper == NULL) {
						pstate->pheader_keeper = header_keeper_alloc(hline, pheader_fields);
						lhmslv_put(pstate->pheader_keepers, pheader_fields, pstate->pheader_keeper);
					} else { // Re-use the header-keeper in the header cache
						slls_free(pheader_fields);
						free(hline);
					}
					break;
				}
			}
		}

		char* line = (pstate->irslen == 1)
			? mlr_get_cline(input_stream, pstate->irs[0])
			: mlr_get_sline(input_stream, pstate->irs, pstate->irslen);
		if (line == NULL) // EOF
			return NULL;

		// xxx empty-line check ... make a lib func is_empty_modulo_whitespace().
		if (!*line) {
			if (pstate->pheader_keeper != NULL) {
				pstate->pheader_keeper = NULL;
				pstate->expect_header_line_next = TRUE;
				free(line);
				continue;
			}
		} else {
			pstate->ifnr++;
			return (pstate->ifslen == 1)
				?  lrec_parse_stdio_csvlite_data_line_single_ifs(pstate->pheader_keeper, line,
						pstate->ifs[0], pstate->allow_repeat_ifs)
				:  lrec_parse_stdio_csvlite_data_line_multi_ifs(pstate->pheader_keeper, line,
						pstate->ifs, pstate->ifslen, pstate->allow_repeat_ifs);
		}
	}
}

// ----------------------------------------------------------------
static void lrec_reader_stdio_sof(void* pvstate) {
	lrec_reader_stdio_csvlite_state_t* pstate = pvstate;
	pstate->ifnr = 0LL;
	pstate->ilno = 0LL;
	pstate->expect_header_line_next = TRUE;
}

// ----------------------------------------------------------------
static void lrec_reader_stdio_csvlite_free(void* pvstate) {
	lrec_reader_stdio_csvlite_state_t* pstate = pvstate;
	for (lhmslve_t* pe = pstate->pheader_keepers->phead; pe != NULL; pe = pe->pnext) {
		header_keeper_t* pheader_keeper = pe->pvvalue;
		header_keeper_free(pheader_keeper);
	}
}

// ----------------------------------------------------------------
lrec_reader_t* lrec_reader_stdio_csvlite_alloc(char* irs, char* ifs, int allow_repeat_ifs) {
	lrec_reader_t* plrec_reader = mlr_malloc_or_die(sizeof(lrec_reader_t));

	lrec_reader_stdio_csvlite_state_t* pstate = mlr_malloc_or_die(sizeof(lrec_reader_stdio_csvlite_state_t));
	pstate->ifnr                      = 0LL;
	pstate->irs                       = irs;
	pstate->ifs                       = ifs;
	pstate->irslen                    = strlen(irs);
	pstate->ifslen                    = strlen(ifs);
	pstate->allow_repeat_ifs          = allow_repeat_ifs;
	pstate->expect_header_line_next   = TRUE;
	pstate->pheader_keeper            = NULL;
	pstate->pheader_keepers           = lhmslv_alloc();

	plrec_reader->pvstate       = (void*)pstate;
	plrec_reader->popen_func    = &file_reader_stdio_vopen;
	plrec_reader->pclose_func   = &file_reader_stdio_vclose;
	plrec_reader->pprocess_func = &lrec_reader_stdio_csvlite_process;
	plrec_reader->psof_func     = &lrec_reader_stdio_sof;
	plrec_reader->pfree_func    = &lrec_reader_stdio_csvlite_free;

	return plrec_reader;
}

// ----------------------------------------------------------------
lrec_t* lrec_parse_stdio_csvlite_data_line_single_ifs(header_keeper_t* pheader_keeper, char* data_line, char ifs,
	int allow_repeat_ifs)
{
	lrec_t* prec = lrec_csvlite_alloc(data_line);
	char* p = data_line;

	if (allow_repeat_ifs) {
		while (*p == ifs)
			p++;
	}
	char* key   = NULL;
	char* value = p;

	// xxx needs pe-non-null (hdr-empty) check:
	sllse_t* pe = pheader_keeper->pkeys->phead;
	for ( ; *p; ) {
		if (*p == ifs) {
			*p = 0;

			if (pe == NULL) { // xxx to do: get file-name/line-number context in here
				fprintf(stderr, "Header-data length mismatch!\n");
				exit(1);
			}
			key = pe->value;
			lrec_put_no_free(prec, key, value);

			p++;
			if (allow_repeat_ifs) {
				while (*p == ifs)
					p++;
			}
			value = p;
			pe = pe->pnext;
		} else {
			p++;
		}
	}
	if (pe == NULL) {
		fprintf(stderr, "Header-data length mismatch!\n");
		exit(1);
	}
	if (allow_repeat_ifs && *value == 0) {
		; // OK
	} else {
		key = pe->value;
		lrec_put_no_free(prec, key, value);
		if (pe->pnext != NULL) {
			fprintf(stderr, "Header-data length mismatch!\n");
			exit(1);
		}
	}

	return prec;
}

lrec_t* lrec_parse_stdio_csvlite_data_line_multi_ifs(header_keeper_t* pheader_keeper, char* data_line,
	char* ifs, int ifslen, int allow_repeat_ifs)
{
	lrec_t* prec = lrec_csvlite_alloc(data_line);
	char* p = data_line;

	if (allow_repeat_ifs) {
		while (streqn(p, ifs, ifslen))
			p += ifslen;
	}
	char* key   = NULL;
	char* value = p;

	// xxx needs pe-non-null (hdr-empty) check:
	sllse_t* pe = pheader_keeper->pkeys->phead;
	for ( ; *p; ) {
		if (streqn(p, ifs, ifslen)) {
			*p = 0;

			if (pe == NULL) { // xxx to do: get file-name/line-number context in here
				fprintf(stderr, "Header-data length mismatch!\n");
				exit(1);
			}
			key = pe->value;
			lrec_put_no_free(prec, key, value);

			p += ifslen;
			if (allow_repeat_ifs) {
				while (streqn(p, ifs, ifslen))
					p += ifslen;
			}
			value = p;
			pe = pe->pnext;
		} else {
			p++;
		}
	}
	if (pe == NULL) {
		fprintf(stderr, "Header-data length mismatch!\n");
		exit(1);
	}
	if (allow_repeat_ifs && *value == 0) {
		; // OK
	} else {
		key = pe->value;
		lrec_put_no_free(prec, key, value);
		if (pe->pnext != NULL) {
			fprintf(stderr, "Header-data length mismatch!\n");
			exit(1);
		}
	}

	return prec;
}

// ----------------------------------------------------------------
slls_t* split_csvlite_header_line_single_ifs(char* line, char ifs, int allow_repeat_ifs) {
	slls_t* plist = slls_alloc();
	if (*line == 0) // empty string splits to empty list
		return plist;

	char* p = line;
	if (allow_repeat_ifs) {
		while (*p == ifs)
			p++;
	}
	char* start = p;
	for ( ; *p; p++) {
		if (*p == ifs) {
			*p = 0;
			p++;
			// xxx hoist loop invariant at the cost of some code duplication
			if (allow_repeat_ifs) {
				while (*p == ifs)
					p++;
			}
			slls_add_no_free(plist, start);
			start = p;
		}
	}
	slls_add_no_free(plist, start);

	return plist;
}

slls_t* split_csvlite_header_line_multi_ifs(char* line, char* ifs, int ifslen, int allow_repeat_ifs) {
	slls_t* plist = slls_alloc();
	if (*line == 0) // empty string splits to empty list
		return plist;

	char* p = line;
	if (allow_repeat_ifs) {
		while (streqn(p, ifs, ifslen))
			p += ifslen;
	}
	char* start = p;
	for ( ; *p; p++) {
		if (streqn(p, ifs, ifslen)) {
			*p = 0;
			p += ifslen;
			if (allow_repeat_ifs) {
				while (streqn(p, ifs, ifslen))
					p += ifslen;
			}
			slls_add_no_free(plist, start);
			start = p;
		}
	}
	slls_add_no_free(plist, start);

	return plist;
}

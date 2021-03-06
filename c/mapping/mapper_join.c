#include "lib/mlr_globals.h"
#include "lib/mlrutil.h"
#include "containers/lrec.h"
#include "containers/sllv.h"
#include "containers/lhmslv.h"
#include "containers/mixutil.h"
#include "containers/join_bucket_keeper.h"
#include "mapping/mappers.h"
#include "input/lrec_readers.h"
#include "cli/argparse.h"

// Join options, if unspecified, default to respective main options.
#define OPTION_UNSPECIFIED ((char)0xff)

// ----------------------------------------------------------------

typedef struct _mapper_join_opts_t {
	char*    left_prefix;
	char*    right_prefix;
	slls_t*  pleft_join_field_names;
	slls_t*  pright_join_field_names;
	slls_t*  poutput_join_field_names;
	int      allow_unsorted_input;
	int      emit_pairables;
	int      emit_left_unpairables;
	int      emit_right_unpairables;

	char*    left_file_name;

	// These allow the joiner to have its own different format/delimiter for
	// the left-file:
	char*    input_file_format;
	char*    irs;
	char*    ifs;
	char*    ips;
	int      allow_repeat_ifs;
	int      allow_repeat_ips;
	char*    ifile_fmt;
	int      use_mmap_for_read;
} mapper_join_opts_t;

typedef struct _mapper_join_state_t {
	mapper_join_opts_t* popts;

	hss_t*   pleft_field_name_set;
	hss_t*   pright_field_name_set;

	// For sorted input
	join_bucket_keeper_t* pjoin_bucket_keeper;

	// For unsorted input
	lhmslv_t* pleft_buckets_by_join_field_values;

} mapper_join_state_t;

static void merge_options(mapper_join_opts_t* popts);
static void ingest_left_file(mapper_join_state_t* pstate);
static void mapper_join_form_pairs(sllv_t* pleft_records, lrec_t* pright_rec, mapper_join_state_t* pstate,
	sllv_t* pout_recs);

// ----------------------------------------------------------------
static sllv_t* mapper_join_process_sorted(lrec_t* pright_rec, context_t* pctx, void* pvstate) {
	mapper_join_state_t* pstate = (mapper_join_state_t*)pvstate;

	// This can't be done in the CLI-parser since it requires information which
	// isn't known until after the CLI-parser is called.
	if (pstate->pjoin_bucket_keeper == NULL) {
		mapper_join_opts_t* popts = pstate->popts;
		merge_options(pstate->popts);
		pstate->pjoin_bucket_keeper = join_bucket_keeper_alloc(
			popts->left_file_name,
			popts->input_file_format,
			popts->use_mmap_for_read,
			popts->irs,
			popts->ifs,
			popts->allow_repeat_ifs,
			popts->ips,
			popts->allow_repeat_ips,
			popts->pleft_join_field_names);
	}
	join_bucket_keeper_t* pkeeper = pstate->pjoin_bucket_keeper; // keystroke-saver

	sllv_t* pleft_records = NULL;
	sllv_t* pbucket_left_unpaired = NULL;
	sllv_t* pout_recs = sllv_alloc();

	if (pright_rec == NULL) { // End of input record stream
		join_bucket_keeper_emit(pkeeper, NULL, &pleft_records, &pbucket_left_unpaired);
		if (pstate->popts->emit_left_unpairables) {
			sllv_add_all(pout_recs, pbucket_left_unpaired);
		}
		sllv_free(pbucket_left_unpaired);
		sllv_add(pout_recs, NULL);
		return pout_recs;
	}

	slls_t* pright_field_values = mlr_selected_values_from_record(pright_rec, pstate->popts->pright_join_field_names);
	join_bucket_keeper_emit(pkeeper, pright_field_values, &pleft_records, &pbucket_left_unpaired);

	if (pstate->popts->emit_left_unpairables) {
		if (pbucket_left_unpaired != NULL && pbucket_left_unpaired->length >= 0) {
			sllv_add_all(pout_recs, pbucket_left_unpaired);
			sllv_free(pbucket_left_unpaired);
		}
	}

	if (pstate->popts->emit_right_unpairables) {
		if (pleft_records == NULL || pleft_records->length == 0) {
			sllv_add(pout_recs, pright_rec);
		}
	}

	if (pstate->popts->emit_pairables && pleft_records != NULL) {
		mapper_join_form_pairs(pleft_records, pright_rec, pstate, pout_recs);
	}

	return pout_recs;
}

// ----------------------------------------------------------------
static sllv_t* mapper_join_process_unsorted(lrec_t* pright_rec, context_t* pctx, void* pvstate) {
	mapper_join_state_t* pstate = (mapper_join_state_t*)pvstate;

	// This can't be done in the CLI-parser since it requires information which
	// isn't known until after the CLI-parser is called.
	if (pstate->pleft_buckets_by_join_field_values == NULL) // First call
		ingest_left_file(pstate);

	if (pright_rec == NULL) { // End of input record stream
		if (pstate->popts->emit_left_unpairables) {
			sllv_t* poutrecs = sllv_alloc();
			if (pstate->pleft_buckets_by_join_field_values != NULL) { // E.g. empty right input
				for (lhmslve_t* pe = pstate->pleft_buckets_by_join_field_values->phead; pe != NULL; pe = pe->pnext) {
					join_bucket_t* pbucket = pe->pvvalue;
					if (!pbucket->was_paired) {
						sllv_add_all(poutrecs, pbucket->precords);
					}
				}
			}
			sllv_add(poutrecs, NULL);
			return poutrecs;
		} else {
			return sllv_single(NULL);
		}
	}

	slls_t* pright_field_values = mlr_selected_values_from_record(pright_rec, pstate->popts->pright_join_field_names);
	join_bucket_t* pleft_bucket = lhmslv_get(pstate->pleft_buckets_by_join_field_values, pright_field_values);
	if (pleft_bucket == NULL) {
		if (pstate->popts->emit_right_unpairables) {
			return sllv_single(pright_rec);
		} else {
			return NULL;
		}
	} else if (pstate->popts->emit_pairables) {
		sllv_t* pout_recs = sllv_alloc();
		pleft_bucket->was_paired = TRUE;
		mapper_join_form_pairs(pleft_bucket->precords, pright_rec, pstate, pout_recs);
		return pout_recs;
	} else {
		pleft_bucket->was_paired = TRUE;
		return NULL;
	}
}

// ----------------------------------------------------------------
// This could be optimized in several ways:
// * Store the prefix length instead of computing its strlen inside
//   mlr_paste_2_strings
// * Don't do the if-statement on each call: prefix is null or non-null
//   at the time the mapper is constructed. Use a function pointer.
static inline char* compose_keys(char* prefix, char* key) {
	if (prefix == NULL)
		return strdup(key);
	else
		return mlr_paste_2_strings(prefix, key);
}

static void mapper_join_form_pairs(sllv_t* pleft_records, lrec_t* pright_rec, mapper_join_state_t* pstate,
	sllv_t* pout_recs)
{
	for (sllve_t* pe = pleft_records->phead; pe != NULL; pe = pe->pnext) {
		lrec_t* pleft_rec = pe->pvdata;
		lrec_t* pout_rec = lrec_unbacked_alloc();

		// add the joined-on fields
		sllse_t* pg = pstate->popts->pleft_join_field_names->phead;
		sllse_t* ph = pstate->popts->pright_join_field_names->phead;
		sllse_t* pi = pstate->popts->poutput_join_field_names->phead;
		for ( ; pg != NULL && ph != NULL && pi != NULL; pg = pg->pnext, ph = ph->pnext, pi = pi->pnext) {
			char* v = lrec_get(pleft_rec, pg->value);
			if (v != NULL) {
				lrec_put(pout_rec, pi->value, strdup(v), LREC_FREE_ENTRY_VALUE);
			}
		}

		// add the left-record fields not already added
		for (lrece_t* pl = pleft_rec->phead; pl != NULL; pl = pl->pnext) {
			if (!hss_has(pstate->pleft_field_name_set, pl->key)) {
				lrec_put(pout_rec, compose_keys(pstate->popts->left_prefix, pl->key),
					strdup(pl->value), LREC_FREE_ENTRY_KEY|LREC_FREE_ENTRY_VALUE);
			}
		}

		// add the right-record fields not already added
		for (lrece_t* pr = pright_rec->phead; pr != NULL; pr = pr->pnext) {
			if (!hss_has(pstate->pright_field_name_set, pr->key)) {
				lrec_put(pout_rec, compose_keys(pstate->popts->right_prefix, pr->key),
					strdup(pr->value), LREC_FREE_ENTRY_KEY|LREC_FREE_ENTRY_VALUE);
			}
		}

		sllv_add(pout_recs, pout_rec);
	}
}

// ----------------------------------------------------------------
static void mapper_join_free(void* pvstate) {
	mapper_join_state_t* pstate = (mapper_join_state_t*)pvstate;
	if (pstate->popts->pleft_join_field_names != NULL)
		slls_free(pstate->popts->pleft_join_field_names);
	if (pstate->popts->pright_join_field_names != NULL)
		slls_free(pstate->popts->pright_join_field_names);
	if (pstate->popts->poutput_join_field_names != NULL)
		slls_free(pstate->popts->poutput_join_field_names);
	if (pstate->pjoin_bucket_keeper != NULL)
		join_bucket_keeper_free(pstate->pjoin_bucket_keeper);
}

// ----------------------------------------------------------------
// Format and separator flags are passed to mapper_join in MLR_GLOBALS rather
// than on the stack, since the latter would require complicating the interface
// for all the other mappers which don't do their own file I/O.  (Also, while
// some of the information needed to construct an lrec_reader is available on
// the command line before the mapper-allocators are called, some is not
// available until after.  Hence our obtaining these flags after mapper-alloc.)

static void merge_options(mapper_join_opts_t* popts) {
	if (popts->input_file_format == NULL)
		popts->input_file_format = MLR_GLOBALS.popts->ifile_fmt;
	if (popts->irs               == NULL)
		popts->irs = MLR_GLOBALS.popts->irs;
	if (popts->ifs               == NULL)
		popts->ifs = MLR_GLOBALS.popts->ifs;
	if (popts->ips               == NULL)
		popts->ips = MLR_GLOBALS.popts->ips;
	if (popts->allow_repeat_ifs  == OPTION_UNSPECIFIED)
		popts->allow_repeat_ifs = MLR_GLOBALS.popts->allow_repeat_ifs;
	if (popts->allow_repeat_ips  == OPTION_UNSPECIFIED)
		popts->allow_repeat_ips = MLR_GLOBALS.popts->allow_repeat_ips;
	if (popts->use_mmap_for_read == OPTION_UNSPECIFIED)
		popts->use_mmap_for_read = MLR_GLOBALS.popts->use_mmap_for_read;
}

static void ingest_left_file(mapper_join_state_t* pstate) {
	mapper_join_opts_t* popts = pstate->popts;
	merge_options(popts);

	lrec_reader_t* plrec_reader = lrec_reader_alloc(popts->input_file_format, popts->use_mmap_for_read,
		popts->irs, popts->ifs, popts->allow_repeat_ifs, popts->ips, popts->allow_repeat_ips);

	void* pvhandle = plrec_reader->popen_func(plrec_reader->pvstate, pstate->popts->left_file_name);
	plrec_reader->psof_func(plrec_reader->pvstate);

	context_t ctx = { .nr = 0, .fnr = 0, .filenum = 1, .filename = pstate->popts->left_file_name };
	context_t* pctx = &ctx;

	pstate->pleft_buckets_by_join_field_values = lhmslv_alloc();

	while (TRUE) {
		lrec_t* pleft_rec = plrec_reader->pprocess_func(plrec_reader->pvstate, pvhandle, pctx);
		if (pleft_rec == NULL)
			break;

		slls_t* pleft_field_values = mlr_selected_values_from_record(pleft_rec, pstate->popts->pleft_join_field_names);
		join_bucket_t* pbucket = lhmslv_get(pstate->pleft_buckets_by_join_field_values, pleft_field_values);
		if (pbucket == NULL) { // New key-field-value: new bucket and hash-map entry
			slls_t* pkey_field_values_copy = slls_copy(pleft_field_values);
			join_bucket_t* pbucket = mlr_malloc_or_die(sizeof(join_bucket_t));
			pbucket->precords = sllv_alloc();
			pbucket->was_paired = FALSE;
			sllv_add(pbucket->precords, pleft_rec);
			lhmslv_put(pstate->pleft_buckets_by_join_field_values, pkey_field_values_copy, pbucket);
		} else { // Previously seen key-field-value: append record to bucket
			sllv_add(pbucket->precords, pleft_rec);
		}
	}

	plrec_reader->pclose_func(plrec_reader->pvstate, pvhandle);
}

// ----------------------------------------------------------------
static mapper_t* mapper_join_alloc(mapper_join_opts_t* popts)
{
	mapper_t* pmapper = mlr_malloc_or_die(sizeof(mapper_t));

	mapper_join_state_t* pstate = mlr_malloc_or_die(sizeof(mapper_join_state_t));
	pstate->popts                              = popts;
	pstate->pleft_field_name_set               = hss_from_slls(popts->pleft_join_field_names);
	pstate->pright_field_name_set              = hss_from_slls(popts->pright_join_field_names);
	pstate->pleft_buckets_by_join_field_values = NULL;
	pstate->pjoin_bucket_keeper                = NULL;

	pmapper->pvstate = (void*)pstate;
	if (popts->allow_unsorted_input) {
		pmapper->pprocess_func = mapper_join_process_unsorted;
	} else {
		pmapper->pprocess_func = mapper_join_process_sorted;
	}
	pmapper->pfree_func = mapper_join_free;

	return pmapper;
}

// ----------------------------------------------------------------
static void mapper_join_usage(char* argv0, char* verb) {
	fprintf(stdout, "Usage: %s %s [options]\n", argv0, verb);
	fprintf(stdout, "Joins records from specified left file name with records from all file names\n");
	fprintf(stdout, "at the end of the Miller argument list.\n");
	fprintf(stdout, "Functionality is essentially the same as the system \"join\" command, but for\n");
	fprintf(stdout, "record streams.\n");
	fprintf(stdout, "Options:\n");
	fprintf(stdout, "  -f {left file name}\n");
	fprintf(stdout, "  -j {a,b,c}   Comma-separated join-field names for output\n");
	fprintf(stdout, "  -l {a,b,c}   Comma-separated join-field names for left input file;\n");
	fprintf(stdout, "               defaults to -j values if omitted.\n");
	fprintf(stdout, "  -r {a,b,c}   Comma-separated join-field names for right input file(s);\n");
	fprintf(stdout, "               defaults to -j values if omitted.\n");
	fprintf(stdout, "  --lp {text}  Additional prefix for non-join output field names from\n");
	fprintf(stdout, "               the left file\n");
	fprintf(stdout, "  --rp {text}  Additional prefix for non-join output field names from\n");
	fprintf(stdout, "               the right file(s)\n");
	fprintf(stdout, "  --np         Do not emit paired records\n");
	fprintf(stdout, "  --ul         Emit unpaired records from the left file\n");
	fprintf(stdout, "  --ur         Emit unpaired records from the right file(s)\n");
	fprintf(stdout, "  -u           Enable unsorted input. In this case, the entire left file will\n");
	fprintf(stdout, "               be loaded into memory. Without -u, records must be sorted\n");
	fprintf(stdout, "               lexically by their join-field names, else not all records will\n");
	fprintf(stdout, "               be paired.\n");
	fprintf(stdout, "File-format options default to those for the right file names on the Miller\n");
	fprintf(stdout, "argument list, but may be overridden for the left file as follows. Please see\n");
	fprintf(stdout, "the main \"%s --help\" for more information on syntax for these arguments.\n", argv0);

	fprintf(stdout, "  -i {one of csv,dkvp,nidx,pprint,xtab}\n");
	fprintf(stdout, "  --irs {record-separator character}\n");
	fprintf(stdout, "  --ifs {field-separator character}\n");
	fprintf(stdout, "  --ips {pair-separator character}\n");
	fprintf(stdout, "  --repifs\n");
	fprintf(stdout, "  --repips\n");
	fprintf(stdout, "  --use-mmap\n");
	fprintf(stdout, "  --no-mmap\n");

	fprintf(stdout, "Please see http://johnkerl.org/miller/doc/reference.html for more information\n");
	fprintf(stdout, "including examples.\n");
}

// ----------------------------------------------------------------
static mapper_t* mapper_join_parse_cli(int* pargi, int argc, char** argv) {
	mapper_join_opts_t* popts = mlr_malloc_or_die(sizeof(mapper_join_opts_t));
	popts->left_prefix              = NULL;
	popts->right_prefix             = NULL;
	popts->left_file_name           = NULL;
	popts->poutput_join_field_names = NULL;
	popts->pleft_join_field_names   = NULL;
	popts->pright_join_field_names  = NULL;
	popts->allow_unsorted_input     = FALSE;
	popts->emit_pairables           = TRUE;
	popts->emit_left_unpairables    = FALSE;
	popts->emit_right_unpairables   = FALSE;

	popts->input_file_format = NULL;
	popts->irs               = NULL;
	popts->ifs               = NULL;
	popts->ips               = NULL;
	popts->allow_repeat_ifs  = OPTION_UNSPECIFIED;
	popts->allow_repeat_ips  = OPTION_UNSPECIFIED;
	popts->use_mmap_for_read = OPTION_UNSPECIFIED;

	char* verb = argv[(*pargi)++];

	ap_state_t* pstate = ap_alloc();
	ap_define_string_flag(pstate,      "-f",         &popts->left_file_name);
	ap_define_string_list_flag(pstate, "-j",         &popts->poutput_join_field_names);
	ap_define_string_list_flag(pstate, "-l",         &popts->pleft_join_field_names);
	ap_define_string_list_flag(pstate, "-r",         &popts->pright_join_field_names);
	ap_define_string_flag(pstate,      "--lp",       &popts->left_prefix);
	ap_define_string_flag(pstate,      "--rp",       &popts->right_prefix);
	ap_define_false_flag(pstate,       "--np",       &popts->emit_pairables);
	ap_define_true_flag(pstate,        "--ul",       &popts->emit_left_unpairables);
	ap_define_true_flag(pstate,        "--ur",       &popts->emit_right_unpairables);
	ap_define_true_flag(pstate,        "-u",         &popts->allow_unsorted_input);

	ap_define_string_flag(pstate,      "-i",         &popts->input_file_format);
	ap_define_string_flag(pstate,      "--irs",      &popts->irs);
	ap_define_string_flag(pstate,      "--ifs",      &popts->ifs);
	ap_define_string_flag(pstate,      "--ips",      &popts->ips);
	ap_define_true_flag(pstate,        "--repifs",   &popts->allow_repeat_ifs);
	ap_define_true_flag(pstate,        "--repips",   &popts->allow_repeat_ips);
	ap_define_true_flag(pstate,        "--use-mmap", &popts->use_mmap_for_read);
	ap_define_false_flag(pstate,       "--no-mmap",  &popts->use_mmap_for_read);

	if (!ap_parse(pstate, verb, pargi, argc, argv)) {
		mapper_join_usage(argv[0], verb);
		return NULL;
	}

	if (popts->left_file_name == NULL) {
		fprintf(stderr, "%s %s: need left file name\n", MLR_GLOBALS.argv0, verb);
		mapper_join_usage(argv[0], verb);
		return NULL;
	}

	if (!popts->emit_pairables && !popts->emit_left_unpairables && !popts->emit_right_unpairables) {
		fprintf(stderr, "%s %s: all emit flags are unset; no output is possible.\n",
			MLR_GLOBALS.argv0, verb);
		mapper_join_usage(argv[0], verb);
		return NULL;
	}

	if (popts->poutput_join_field_names == NULL) {
		fprintf(stderr, "%s %s: need output field names\n", MLR_GLOBALS.argv0, verb);
		mapper_join_usage(argv[0], verb);
		return NULL;
	}
	if (popts->pleft_join_field_names == NULL)
		popts->pleft_join_field_names = slls_copy(popts->poutput_join_field_names);
	if (popts->pright_join_field_names == NULL)
		popts->pright_join_field_names = slls_copy(popts->pleft_join_field_names);

	int llen = popts->pleft_join_field_names->length;
	int rlen = popts->pright_join_field_names->length;
	int olen = popts->poutput_join_field_names->length;
	if (llen != rlen || llen != olen) {
		fprintf(stderr,
			"%s %s: must have equal left,right,output field-name lists; got lengths %d,%d,%d.\n",
			MLR_GLOBALS.argv0, verb, llen, rlen, olen);
		exit(1);
	}

	return mapper_join_alloc(popts);
}

// ----------------------------------------------------------------
mapper_setup_t mapper_join_setup = {
	.verb = "join",
	.pusage_func = mapper_join_usage,
	.pparse_func = mapper_join_parse_cli,
};

#include <stdio.h>
#include <string.h>
#include "lib/minunit.h"
#include "lib/mlrutil.h"
#include "containers/slls.h"
#include "containers/sllv.h"
#include "containers/hss.h"
#include "containers/lhmsi.h"
#include "containers/lhms2v.h"
#include "containers/lhmslv.h"
#include "containers/top_keeper.h"
#include "containers/dheap.h"

#ifdef __TEST_MAPS_AND_SETS_MAIN__
int tests_run         = 0;
int tests_failed      = 0;
int assertions_run    = 0;
int assertions_failed = 0;

// ----------------------------------------------------------------
static char* test_slls() {
	slls_t* plist = slls_from_line(strdup(""), ',', FALSE);
	mu_assert_lf(plist->length == 0);

	plist = slls_from_line(strdup("a"), ',', FALSE);
	mu_assert_lf(plist->length == 1);

	plist = slls_from_line(strdup("c,d,a,e,b"), ',', FALSE);
	mu_assert_lf(plist->length == 5);

	sllse_t* pe = plist->phead;

	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "c")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "d")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "a")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "e")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "b")); pe = pe->pnext;
	mu_assert_lf(pe == NULL);

	slls_sort(plist);

	mu_assert_lf(plist->length == 5);
	pe = plist->phead;

	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "a")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "b")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "c")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "d")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->value, "e")); pe = pe->pnext;
	mu_assert_lf(pe == NULL);

	return NULL;
}

// ----------------------------------------------------------------
static char* test_sllv_append() {
	mu_assert_lf(0 == 0);

	sllv_t* pa = sllv_alloc();
	sllv_add(pa, "a");
	sllv_add(pa, "b");
	sllv_add(pa, "c");
	mu_assert_lf(pa->length == 3);

	sllve_t* pe = pa->phead;

	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "a")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "b")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "c")); pe = pe->pnext;
	mu_assert_lf(pe == NULL);

	sllv_t* pb = sllv_alloc();
	sllv_add(pb, "d");
	sllv_add(pb, "e");
	mu_assert_lf(pb->length == 2);

	pe = pb->phead;

	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "d")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "e")); pe = pe->pnext;
	mu_assert_lf(pe == NULL);

	pa = sllv_append(pa, pb);

	mu_assert_lf(pa->length == 5);
	mu_assert_lf(pb->length == 2);

	pe = pa->phead;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "a")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "b")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "c")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "d")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "e")); pe = pe->pnext;
	mu_assert_lf(pe == NULL);

	pe = pb->phead;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "d")); pe = pe->pnext;
	mu_assert_lf(pe != NULL); mu_assert_lf(streq(pe->pvdata, "e")); pe = pe->pnext;
	mu_assert_lf(pe == NULL);

	return NULL;
}

// ----------------------------------------------------------------
static char* test_hss() {

	hss_t *pset = hss_alloc();
	mu_assert_lf(pset->num_occupied == 0);

	hss_add(pset, "x");
	mu_assert_lf(pset->num_occupied == 1);
	mu_assert_lf(!hss_has(pset, "w"));
	mu_assert_lf(hss_has(pset, "x"));
	mu_assert_lf(!hss_has(pset, "y"));
	mu_assert_lf(!hss_has(pset, "z"));
	mu_assert_lf(hss_check_counts(pset));

	hss_add(pset, "y");
	mu_assert_lf(pset->num_occupied == 2);
	mu_assert_lf(!hss_has(pset, "w"));
	mu_assert_lf(hss_has(pset, "x"));
	mu_assert_lf(hss_has(pset, "y"));
	mu_assert_lf(!hss_has(pset, "z"));
	mu_assert_lf(hss_check_counts(pset));

	hss_add(pset, "x");
	mu_assert_lf(pset->num_occupied == 2);
	mu_assert_lf(!hss_has(pset, "w"));
	mu_assert_lf(hss_has(pset, "x"));
	mu_assert_lf(hss_has(pset, "y"));
	mu_assert_lf(!hss_has(pset, "z"));
	mu_assert_lf(hss_check_counts(pset));

	hss_add(pset, "z");
	mu_assert_lf(pset->num_occupied == 3);
	mu_assert_lf(!hss_has(pset, "w"));
	mu_assert_lf(hss_has(pset, "x"));
	mu_assert_lf(hss_has(pset, "y"));
	mu_assert_lf(hss_has(pset, "z"));
	mu_assert_lf(hss_check_counts(pset));

	hss_remove(pset, "y");
	mu_assert_lf(pset->num_occupied == 2);
	mu_assert_lf(!hss_has(pset, "w"));
	mu_assert_lf(hss_has(pset, "x"));
	mu_assert_lf(!hss_has(pset, "y"));
	mu_assert_lf(hss_has(pset, "z"));
	mu_assert_lf(hss_check_counts(pset));

	hss_clear(pset);
	mu_assert_lf(!hss_has(pset, "w"));
	mu_assert_lf(!hss_has(pset, "x"));
	mu_assert_lf(!hss_has(pset, "y"));
	mu_assert_lf(!hss_has(pset, "z"));
	mu_assert_lf(hss_check_counts(pset));

	hss_free(pset);

	return NULL;
}

// ----------------------------------------------------------------
static char* test_lhmsi() {
	mu_assert_lf(0 == 0);

	lhmsi_t *pmap = lhmsi_alloc();
	mu_assert_lf(pmap->num_occupied == 0);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(!lhmsi_has_key(pmap, "x"));
	mu_assert_lf(!lhmsi_has_key(pmap, "y"));
	mu_assert_lf(!lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_put(pmap, "x", 3);
	mu_assert_lf(pmap->num_occupied == 1);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(lhmsi_has_key(pmap, "x"));
	mu_assert_lf(!lhmsi_has_key(pmap, "y"));
	mu_assert_lf(!lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_put(pmap, "y", 5);
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(lhmsi_has_key(pmap, "x"));
	mu_assert_lf(lhmsi_has_key(pmap, "y"));
	mu_assert_lf(!lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_put(pmap, "x", 4);
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(lhmsi_has_key(pmap, "x"));
	mu_assert_lf(lhmsi_has_key(pmap, "y"));
	mu_assert_lf(!lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_put(pmap, "z", 7);
	mu_assert_lf(pmap->num_occupied == 3);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(lhmsi_has_key(pmap, "x"));
	mu_assert_lf(lhmsi_has_key(pmap, "y"));
	mu_assert_lf(lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_remove(pmap, "y");
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(lhmsi_has_key(pmap, "x"));
	mu_assert_lf(!lhmsi_has_key(pmap, "y"));
	mu_assert_lf(lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_clear(pmap);
	mu_assert_lf(pmap->num_occupied == 0);
	mu_assert_lf(!lhmsi_has_key(pmap, "w"));
	mu_assert_lf(!lhmsi_has_key(pmap, "x"));
	mu_assert_lf(!lhmsi_has_key(pmap, "y"));
	mu_assert_lf(!lhmsi_has_key(pmap, "z"));
	mu_assert_lf(lhmsi_check_counts(pmap));

	lhmsi_free(pmap);

	return NULL;
}

// ----------------------------------------------------------------
static char* test_lhms2v() {
	mu_assert_lf(0 == 0);

	// xxx more assertions here
	lhms2v_t *pmap = lhms2v_alloc();
	mu_assert_lf(pmap->num_occupied == 0);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_put(pmap, "a", "x", "3");
	mu_assert_lf(pmap->num_occupied == 1);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_put(pmap, "a", "y", "5");
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_put(pmap, "a", "x", "4");
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_put(pmap, "b", "z", "7");
	mu_assert_lf(pmap->num_occupied == 3);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_remove(pmap, "a", "y");
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_clear(pmap);
	mu_assert_lf(pmap->num_occupied == 0);
	mu_assert_lf(lhms2v_check_counts(pmap));

	lhms2v_free(pmap);

	return NULL;
}

// ----------------------------------------------------------------
static char* test_lhmslv() {
	mu_assert_lf(0 == 0);

	slls_t* ax = slls_alloc(); slls_add_no_free(ax, "a"); slls_add_no_free(ax, "x");
	slls_t* ay = slls_alloc(); slls_add_no_free(ay, "a"); slls_add_no_free(ay, "y");
	slls_t* bz = slls_alloc(); slls_add_no_free(bz, "b"); slls_add_no_free(bz, "z");

	// xxx more assertions here
	lhmslv_t *pmap = lhmslv_alloc();
	mu_assert_lf(pmap->num_occupied == 0);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_put(pmap, ax, "3");
	mu_assert_lf(pmap->num_occupied == 1);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_put(pmap, ay, "5");
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_put(pmap, ax, "4");
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_put(pmap, bz, "7");
	mu_assert_lf(pmap->num_occupied == 3);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_remove(pmap, ay);
	mu_assert_lf(pmap->num_occupied == 2);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_clear(pmap);
	mu_assert_lf(pmap->num_occupied == 0);
	mu_assert_lf(lhmslv_check_counts(pmap));

	lhmslv_free(pmap);

	return NULL;
}

// ----------------------------------------------------------------
static char* test_lhmss() {
	mu_assert_lf(0 == 0);

	return NULL;
}

//	lhmss_t *pmap = lhmss_alloc();
//	lhmss_put(pmap, "x", "3");
//	lhmss_put(pmap, "y", "5");
//	lhmss_put(pmap, "x", "4");
//	lhmss_put(pmap, "z", "7");
//	lhmss_remove(pmap, "y");
//	printf("map size = %d\n", pmap->num_occupied);
//	lhmss_dump(pmap);
//	lhmss_check_counts(pmap);
//	lhmss_free(pmap);

// ----------------------------------------------------------------
static char* test_lhmsv() {
	mu_assert_lf(0 == 0);

	return NULL;
}

//	int x3 = 3;
//	int x5 = 5;
//	int x4 = 4;
//	int x7 = 7;
//	lhmsv_t *pmap = lhmsv_alloc();
//	lhmsv_put(pmap, "x", &x3);
//	lhmsv_put(pmap, "y", &x5);
//	lhmsv_put(pmap, "x", &x4);
//	lhmsv_put(pmap, "z", &x7);
//	lhmsv_remove(pmap, "y");
//	printf("map size = %d\n", pmap->num_occupied);
//	lhmsv_dump(pmap);
//	lhmsv_check_counts(pmap);
//	lhmsv_free(pmap);

// ----------------------------------------------------------------
static char* test_percentile_keeper() {
	mu_assert_lf(0 == 0);

	return NULL;
}

//void percentile_keeper_dump(percentile_keeper_t* ppercentile_keeper) {
//	for (int i = 0; i < ppercentile_keeper->size; i++)
//		printf("[%02d] %.8lf\n", i, ppercentile_keeper->data[i]);
//}

//	char buffer[1024];
//	percentile_keeper_t* ppercentile_keeper = percentile_keeper_alloc();
//	char* line;
//	while ((line = fgets(buffer, sizeof(buffer), stdin)) != NULL) {
//		int len = strlen(line);
//		if (len >= 1) // xxx write and use a chomp()
//			if (line[len-1] == '\n')
//				line[len-1] = 0;
//		double v;
//		if (!mlr_try_double_from_string(line, &v)) {
//			percentile_keeper_ingest(ppercentile_keeper, v);
//		} else {
//			printf("meh? >>%s<<\n", line);
//		}
//	}
//	percentile_keeper_dump(ppercentile_keeper);
//	printf("\n");
//	double p;
//	p = 0.10; printf("%.2lf: %.6lf\n", p, percentile_keeper_emit(ppercentile_keeper, p));
//	p = 0.50; printf("%.2lf: %.6lf\n", p, percentile_keeper_emit(ppercentile_keeper, p));
//	p = 0.90; printf("%.2lf: %.6lf\n", p, percentile_keeper_emit(ppercentile_keeper, p));
//	printf("\n");
//	percentile_keeper_dump(ppercentile_keeper);

// ----------------------------------------------------------------
static char* test_top_keeper() {
	mu_assert_lf(0 == 0);
	int capacity = 3;

	top_keeper_t* ptop_keeper = top_keeper_alloc(capacity);
	mu_assert_lf(ptop_keeper->size == 0);

	top_keeper_add(ptop_keeper, 5.0, NULL);
	top_keeper_print(ptop_keeper);
	mu_assert_lf(ptop_keeper->size == 1);
	mu_assert_lf(ptop_keeper->top_values[0] == 5.0);

	top_keeper_add(ptop_keeper, 6.0, NULL);
	top_keeper_print(ptop_keeper);
	mu_assert_lf(ptop_keeper->size == 2);
	mu_assert_lf(ptop_keeper->top_values[0] == 6.0);
	mu_assert_lf(ptop_keeper->top_values[1] == 5.0);

	top_keeper_add(ptop_keeper, 4.0, NULL);
	top_keeper_print(ptop_keeper);
	mu_assert_lf(ptop_keeper->size == 3);
	mu_assert_lf(ptop_keeper->top_values[0] == 6.0);
	mu_assert_lf(ptop_keeper->top_values[1] == 5.0);
	mu_assert_lf(ptop_keeper->top_values[2] == 4.0);

	top_keeper_add(ptop_keeper, 2.0, NULL);
	top_keeper_print(ptop_keeper);
	mu_assert_lf(ptop_keeper->size == 3);
	mu_assert_lf(ptop_keeper->top_values[0] == 6.0);
	mu_assert_lf(ptop_keeper->top_values[1] == 5.0);
	mu_assert_lf(ptop_keeper->top_values[2] == 4.0);

	top_keeper_add(ptop_keeper, 7.0, NULL);
	top_keeper_print(ptop_keeper);
	mu_assert_lf(ptop_keeper->size == 3);
	mu_assert_lf(ptop_keeper->top_values[0] == 7.0);
	mu_assert_lf(ptop_keeper->top_values[1] == 6.0);
	mu_assert_lf(ptop_keeper->top_values[2] == 5.0);

	top_keeper_free(ptop_keeper);
	return NULL;
}

// ----------------------------------------------------------------
static char* test_dheap() {
	mu_assert_lf(0 == 0);

	dheap_t *pdheap = dheap_alloc();
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 0);

	dheap_add(pdheap, 4.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 1);

	dheap_add(pdheap, 3.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 2);

	dheap_add(pdheap, 2.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 3);

	dheap_add(pdheap, 6.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 4);

	dheap_add(pdheap, 5.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 5);

	dheap_add(pdheap, 8.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 6);

	dheap_add(pdheap, 7.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 7);

	dheap_print(pdheap);

	mu_assert_lf(dheap_remove(pdheap) == 8.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 6);

	mu_assert_lf(dheap_remove(pdheap) == 7.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 5);

	mu_assert_lf(dheap_remove(pdheap) == 6.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 4);

	mu_assert_lf(dheap_remove(pdheap) == 5.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 3);

	mu_assert_lf(dheap_remove(pdheap) == 4.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 2);

	mu_assert_lf(dheap_remove(pdheap) == 3.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 1);

	mu_assert_lf(dheap_remove(pdheap) == 2.25);
	mu_assert_lf(dheap_check(pdheap, __FILE__,  __LINE__));
	mu_assert_lf(pdheap->n == 0);

	dheap_free(pdheap);

	return NULL;
}

// ================================================================
static char * run_all_tests() {
	mu_run_test(test_slls);
	mu_run_test(test_sllv_append);
	mu_run_test(test_hss);
	mu_run_test(test_lhmsi);
	mu_run_test(test_lhms2v);
	mu_run_test(test_lhmslv);
	mu_run_test(test_lhmss);
	mu_run_test(test_lhmsv);
	mu_run_test(test_percentile_keeper);
	mu_run_test(test_top_keeper);
	mu_run_test(test_dheap);
	return 0;
}

int main(int argc, char **argv) {
	char *result = run_all_tests();
	printf("\n");
	if (result != 0) {
		printf("Not all unit tests passed\n");
	}
	else {
		printf("TEST_MAPS_AND_SETS: ALL UNIT TESTS PASSED\n");
	}
	printf("Tests      passed: %d of %d\n", tests_run - tests_failed, tests_run);
	printf("Assertions passed: %d of %d\n", assertions_run - assertions_failed, assertions_run);

	return result != 0;
}
#endif // __TEST_MAPS_AND_SETS_MAIN__

/*
 * mixed_negation.c
 *
 */

#include "containers/mixed_negation.h"
#include "bitset_util.h"
#include "array_util.h"
#include "containers/run.h"
#include "containers/containers.h"
#include "containers/convert.h"

#include <assert.h>
#include <string.h>

/* code makes the assumption that sizeof(int) > 2
 * for ranges. Could use uint32_t instead if this is undesirable.
 * But it seems silly to worry about 16-bit platforms with this library.
 */
_Static_assert(sizeof(int) > 2, "ints too small for ranges");

// TODO: make simplified and optimized negation code across
// the full range.

/* Negation across the entire range of the container.
 * Compute the  negation of src  and write the result
 * to *dst. The complement of a
 * sufficiently sparse set will always be dense and a hence a bitmap
' * We assume that dst is pre-allocated and a valid bitset container
 * There can be no in-place version.
 */
void array_container_negation(const array_container_t *src,
                              bitset_container_t *dst) {
    uint64_t card = UINT64_C(1 << 16);
    bitset_container_set_all(dst);

    dst->cardinality =
        bitset_clear_list(dst->array, card, src->array, src->cardinality);
}

/* Negation across the entire range of the container
 * Compute the  negation of src  and write the result
 * to *dst.  A true return value indicates a bitset result,
 * otherwise the result is an array container.
 *  We assume that dst is not pre-allocated. In
 * case of failure, *dst will be NULL.
 */
bool bitset_container_negation(const bitset_container_t *src, void **dst) {
    return bitset_container_negation_range(src, 0, (1 << 16), dst);
}

/* inplace version */
/*
 * Same as bitset_container_negation except that if the output is to
 * be a
 * bitset_container_t, then src is modified and no allocation is made.
 * If the output is to be an array_container_t, then caller is responsible
 * to free the container.
 * In all cases, the result is in *dst.
 */
bool bitset_container_negation_inplace(bitset_container_t *src, void **dst) {
    return bitset_container_negation_range_inplace(src, 0, (1 << 16), dst);
}

/* Negation across the entire range of container
 * Compute the  negation of src  and write the result
 * to *dst.  Return value of 0 indicates an array result,
 * while a 1 indicates an bitset result, and 2 indicates a
 * run result.
 *  We assume that dst is not pre-allocated. In
 * case of failure, *dst will be NULL.
 */
int run_container_negation(const run_container_t *src, void **dst) {
    return run_container_negation_range(src, 0, (1 << 16), dst);
}

/*
 * Same as run_container_negation except that if the output is to
 * be a
 * run_container_t, and has the capacity to hold the result,
 * then src is modified and no allocation is made.
 * In all cases, the result is in *dst.
 */
int run_container_negation_inplace(run_container_t *src, void **dst) {
    return run_container_negation_range(src, 0, (1 << 16), dst);
}

/* Negation across a range of the container.
 * Compute the  negation of src  and write the result
 * to *dst. Returns true if the result is a bitset container
 * and false for an array container.  *dst is not preallocated.
 */
bool array_container_negation_range(const array_container_t *src,
                                    const int range_start, const int range_end,
                                    void **dst) {
    /* close port of the Java implementation */
    if (range_start >= range_end) {
        *dst = array_container_clone(src);
        return false;
    }

    int32_t start_index =
        binarySearch(src->array, src->cardinality, (uint16_t)range_start);
    if (start_index < 0) start_index = -start_index - 1;

    int32_t last_index =
        binarySearch(src->array, src->cardinality, (uint16_t)(range_end - 1));
    if (last_index < 0) last_index = -last_index - 2;

    const int32_t current_values_in_range = last_index - start_index + 1;
    const int32_t span_to_be_flipped = range_end - range_start;
    const int32_t new_values_in_range =
        span_to_be_flipped - current_values_in_range;
    const int32_t cardinality_change =
        new_values_in_range - current_values_in_range;
    const int32_t new_cardinality = src->cardinality + cardinality_change;

    if (new_cardinality > DEFAULT_MAX_SIZE) {
        bitset_container_t *temp = bitset_container_from_array(src);
        bitset_flip_range(temp->array, (uint32_t)range_start,
                          (uint32_t)range_end);
        temp->cardinality = new_cardinality;
        *dst = temp;
        return true;
    }

    array_container_t *arr =
        array_container_create_given_capacity(new_cardinality);
    *dst = (void *)arr;
    // copy stuff before the active area
    memcpy(arr->array, src->array, start_index * sizeof(uint16_t));

    // work on the range
    int32_t out_pos = start_index, in_pos = start_index;
    int32_t val_in_range = range_start;
    for (; val_in_range < range_end && in_pos <= last_index; ++val_in_range) {
        if ((uint16_t)val_in_range != src->array[in_pos]) {
            arr->array[out_pos++] = (uint16_t)val_in_range;
        } else {
            ++in_pos;
        }
    }
    for (; val_in_range < range_end; ++val_in_range)
        arr->array[out_pos++] = (uint16_t)val_in_range;

    // content after the active range
    memcpy(arr->array + out_pos, src->array + (last_index + 1),
           (src->cardinality - (last_index + 1)) * sizeof(uint16_t));
    arr->cardinality = new_cardinality;
    return false;
}

/* Even when the result would fit, it is unclear how to make an
 * inplace version without inefficient copying.
 */

/* Negation across a range of the container
 * Compute the  negation of src  and write the result
 * to *dst.  A true return value indicates a bitset result,
 * otherwise the result is an array container.
 *  We assume that dst is not pre-allocated. In
 * case of failure, *dst will be NULL.
 */
bool bitset_container_negation_range(const bitset_container_t *src,
                                     const int range_start, const int range_end,
                                     void **dst) {
    // TODO maybe consider density-based estimate
    // and sometimes build result directly as array, with
    // conversion back to bitset if wrong.  Or determine
    // actual result cardinality, then go directly for the known final cont.

    // keep computation using bitsets as long as possible.
    bitset_container_t *t = bitset_container_clone(src);
    bitset_flip_range(t->array, (uint32_t)range_start, (uint32_t)range_end);
    t->cardinality = bitset_container_compute_cardinality(t);

    if (t->cardinality > DEFAULT_MAX_SIZE) {
        *dst = t;
        return true;
    } else {
        *dst = array_container_from_bitset(t);
        bitset_container_free(t);
        return false;
    }
}

/* inplace version */
/*
 * Same as bitset_container_negation except that if the output is to
 * be a
 * bitset_container_t, then src is modified and no allocation is made.
 * If the output is to be an array_container_t, then caller is responsible
 * to free the container.
 * In all cases, the result is in *dst.
 */
bool bitset_container_negation_range_inplace(bitset_container_t *src,
                                             const int range_start,
                                             const int range_end, void **dst) {
    bitset_flip_range(src->array, (uint32_t)range_start, (uint32_t)range_end);
    src->cardinality = bitset_container_compute_cardinality(src);
    if (src->cardinality > DEFAULT_MAX_SIZE) {
        *dst = src;
        return true;
    }
    *dst = array_container_from_bitset(src);
    bitset_container_free(src);
    return false;
}

/* Negation across a range of container
 * Compute the  negation of src  and write the result
 * to *dst.  A return value of 0 indicates an array result,
 * while a 1 indicates a bitset result, and 2 indicates a
 * run result.
 *  We assume that dst is not pre-allocated. In
 * case of failure, *dst will be NULL.
 */
int run_container_negation_range(const run_container_t *src,
                                 const int range_start, const int range_end,
                                 void **dst) {
    uint8_t return_typecode;

    // follows the Java implementation
    if (range_end <= range_start) {
        *dst = run_container_clone(src);
        return 2;  // not RUN_CONTAINER_TYPE_CODE;
    }

    run_container_t *ans = run_container_create_given_capacity(
        src->n_runs + 1);  // src->n_runs + 1);
    int k = 0;
    for (; k < src->n_runs && src->runs[k].value < range_start; ++k) {
        ans->runs[k] = src->runs[k];
        ans->n_runs++;
    }

    // temp temp
    // printf("temp temp check1 malloc problem");
    void *fooo = malloc(8192);
    free(fooo);  // survives

    run_container_smart_append_exclusive(
        ans, (uint16_t)range_start, (uint16_t)(range_end - range_start - 1));

    // temp temp
    // printf("temp temp end check3 malloc problem");
    void *foo77 = malloc(8192);  // survives
    free(foo77);

    for (; k < src->n_runs; ++k) {
        run_container_smart_append_exclusive(ans, src->runs[k].value,
                                             src->runs[k].length);
        // temp temp
        // printf("temp temp end check k=%d malloc problem", k);
        void *foo78 = malloc(8192);
        free(foo78);
    }

    // temp temp
    // printf("temp temp end check2 malloc problem");
    void *foo = malloc(8192);  // dies
    free(foo);

    *dst = convert_run_to_efficient_container(ans, &return_typecode);
    printf("return typecode value is %d\n", (int)return_typecode);

    switch (return_typecode) {
        case RUN_CONTAINER_TYPE_CODE:
            return 2;
        case ARRAY_CONTAINER_TYPE_CODE:
            return 0;
        case BITSET_CONTAINER_TYPE_CODE:
        default:
            return 1;
    }
}

/*
 * Same as run_container_negation except that if the output is to
 * be a
 * run_container_t, and has the capacity to hold the result,
 * then src is modified and no allocation is made.
 * In all cases, the result is in *dst.
 */
int run_container_negation_range_inplace(run_container_t *src,
                                         const int range_start,
                                         const int range_end, void **dst) {
    // TODO WRITE
    return 0;
}
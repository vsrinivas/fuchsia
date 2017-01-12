// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.
#include <gpt/gpt.h>
#include <inttypes.h>
#include <limits.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unittest/unittest.h>

#include <installer/lib-installer.h>

#define TABLE_SIZE 6

/*
 * Generate a "random" GUID. Note that you should seed srand() before
 * calling. This is also, by no means, a 'secure' random value being
 * generated.
 */
static void generate_guid(uint8_t* guid_out) {
    static_assert(RAND_MAX == INT_MAX, "Rand max doesn't match int max");
    for (int gen = 0; gen < 16; gen += sizeof(int)) {
        int rand_val = rand();
        if (rand_val > RAND_MAX / 2) {
            rand_val = rand_val - RAND_MAX;
            rand_val *= 2;
        }
        memcpy(guid_out, &rand_val, sizeof(int));
        guid_out += sizeof(int);
    }
}

static void create_partition_table(gpt_partition_t* part_entries_out,
                                   gpt_partition_t** part_entry_table_out,
                                   size_t num_entries,
                                   uint64_t part_size) {
    // sleep a second, just in case we're called consecutively, this will
    // give us a different random seed than any previous call
    sleep(1);
    srand(time(NULL));
    for (size_t idx = 0; idx < num_entries; idx++) {
        part_entry_table_out[idx] = &part_entries_out[idx];
        generate_guid(part_entries_out[idx].type);
        generate_guid(part_entries_out[idx].guid);
        part_entries_out[idx].first = 34 + idx * part_size;
        part_entries_out[idx].last = part_entries_out[idx].first +
            part_size - 1;
    }
}

bool test_find_partition_entries(void) {
    gpt_partition_t* part_entry_ptrs[TABLE_SIZE];
    gpt_partition_t part_entries[TABLE_SIZE];
    //const uint16_t tbl_sz = 6;
    // all partitions are 4GiB worth of 512b blocks
    const uint64_t part_size = ((uint64_t) 1) << (32 - 9);

    create_partition_table(part_entries, part_entry_ptrs, TABLE_SIZE,
                           part_size);

    uint16_t test_indices[3] = {0, TABLE_SIZE - 1, TABLE_SIZE / 2};

    BEGIN_TEST;
    for (uint16_t idx = 0; idx < sizeof(test_indices) / sizeof(test_indices[0]);
         idx++) {
        uint16_t found_idx = TABLE_SIZE;
        uint16_t targ_idx = test_indices[idx];
        mx_status_t rc = find_partition_entries(part_entry_ptrs,
                                                part_entries[targ_idx].type,
                                                TABLE_SIZE, &found_idx);
        ASSERT_EQ(rc, NO_ERROR, "");
    }

    uint8_t random_guid[16];
    generate_guid(random_guid);
    uint16_t found_idx = TABLE_SIZE;
    mx_status_t rc = find_partition_entries(part_entry_ptrs, random_guid,
                                            TABLE_SIZE, &found_idx);
    ASSERT_EQ(rc, ERR_NOT_FOUND, "");
    END_TEST;
}

bool test_find_partition(void) {
    gpt_partition_t* part_entry_ptrs[TABLE_SIZE];
    gpt_partition_t part_entries[TABLE_SIZE];
    const uint64_t block_size = 512;
    const uint64_t part_size = ((uint64_t) 1) << 32;

    create_partition_table(part_entries, part_entry_ptrs, TABLE_SIZE,
                           part_size / block_size);


    uint16_t test_indices[3] = {0, TABLE_SIZE - 1, TABLE_SIZE / 2};

    BEGIN_TEST;
    for (uint16_t idx = 0; idx < sizeof(test_indices) / sizeof(test_indices[0]);
         idx++) {
        uint16_t targ_idx = test_indices[idx];
        uint16_t found_idx = TABLE_SIZE;
        gpt_partition_t* part_info;
        mx_status_t rc = find_partition(part_entry_ptrs,
                                        part_entry_ptrs[targ_idx]->type,
                                        part_size, block_size, "TEST",
                                        TABLE_SIZE, &found_idx, &part_info);
        ASSERT_EQ(rc, NO_ERROR, "");
        ASSERT_EQ(targ_idx, found_idx, "");
        ASSERT_BYTES_EQ((uint8_t*) part_entry_ptrs[targ_idx], (uint8_t*) part_info,
                        sizeof(gpt_partition_t), "");
    }

    //need to pass part_size in bytes, not blocks
    uint16_t found_idx = TABLE_SIZE;
    gpt_partition_t* part_info = NULL;
    mx_status_t rc = find_partition(part_entry_ptrs, part_entry_ptrs[0]->type,
                                    part_size + 1, block_size, "TEST",
                                    TABLE_SIZE, &found_idx, &part_info);

    ASSERT_EQ(rc, ERR_NOT_FOUND, "");
    END_TEST;
}

bool verify_sort(gpt_partition_t** partitions, int count) {
    bool ordered = true;
    for (int idx = 1; idx < count; idx++) {
        if (partitions[idx - 1]->first > partitions[idx]->first) {
            ordered = false;
            printf("Values are not ordered, index: %i--%" PRIu64 "!\n", idx,
                   partitions[idx]->first);
        }
    }
    return ordered;
}

bool do_sort_test(int test_size, uint64_t val_max) {
    gpt_partition_t* values = malloc(test_size * sizeof(gpt_partition_t));
    gpt_partition_t** value_ptrs = malloc(test_size * sizeof(gpt_partition_t*));
    if (values == NULL) {
        fprintf(stderr, "Unable to allocate memory for test\n");
        return false;
    }

    for (int idx = 0; idx < test_size; idx++) {
        bool unique = false;
        while (!unique) {
            double rando = ((double) rand()) / RAND_MAX;
            uint64_t val = rando * val_max;

            (values + idx)->first = val;

            // check the uniqueness of the value since our sort doesn't handle
            // duplicate values
            unique = true;
            for (int idx2 = 0; idx2 < idx; idx2++) {
                if ((values + idx2)->first == val) {
                    unique = false;
                    break;
                }
            }

            value_ptrs[idx] = values + idx;
        }
    }

    gpt_partition_t** sorted_values = sort_partitions(value_ptrs, test_size);
    ASSERT_EQ(verify_sort(sorted_values, test_size), true, "");

    // sort again to test ordered data is handled properly
    sorted_values = sort_partitions(sorted_values, test_size);
    ASSERT_EQ(verify_sort(sorted_values, test_size), true, "");

    free(values);
    free(sorted_values);
    free(value_ptrs);

    return true;
}

bool test_sort(void) {
    BEGIN_TEST;
    // run 20 iterations with 20K elements as a stress test. We also think
    // this should hit all possible code paths
    for (int count = 0; count < 20; count++) {
        do_sort_test(256, 10000000);
        fflush(stdout);
    }
    END_TEST;
}

BEGIN_TEST_CASE(installer_tests)
RUN_TEST(test_find_partition_entries)
RUN_TEST(test_find_partition)
RUN_TEST(test_sort);
END_TEST_CASE(installer_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}

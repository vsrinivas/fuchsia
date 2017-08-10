// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <magenta/boot/bootdata.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <mdi/mdi.h>
#include <unittest/unittest.h>

#include "gen-mdi-test.h"

#define MDI_PATH "/boot/data/mditest.mdi"

static void* mdi_data = NULL;
static size_t mdi_length = 0;

static bool load_mdi(void) {
    BEGIN_TEST;

    int fd = open(MDI_PATH, O_RDONLY);
    EXPECT_GE(fd, 0, "Could not open " MDI_PATH);

    off_t length = lseek(fd, 0, SEEK_END);
    EXPECT_GT(fd, 0, "Could not determine length of " MDI_PATH);
    lseek(fd, 0, SEEK_SET);

    mdi_data = malloc(length);
    EXPECT_NONNULL(mdi_data, "Could not allocate memory to read " MDI_PATH);
    EXPECT_EQ(read(fd, mdi_data, length), length, "Could not read %s\n" MDI_PATH);
    mdi_length = length;

    close(fd);

    bootdata_t* header = mdi_data;
    EXPECT_EQ(header->type, (uint32_t)BOOTDATA_CONTAINER, "invalid bootdata container header");
    EXPECT_EQ(header->extra, BOOTDATA_MAGIC, "bootdata container bad magic");
    EXPECT_GT(header->length, sizeof(*header), "bootdata length too small");

    mdi_data += sizeof(*header);
    mdi_length -= sizeof(*header);
    header = mdi_data;

    EXPECT_EQ(header->type, (uint32_t)BOOTDATA_MDI, "bootdata type not BOOTDATA_MDI");
    EXPECT_EQ(header->length + sizeof(*header), mdi_length, "bootdata length invalid");

    END_TEST;
}

bool simple_tests(void) {
    BEGIN_TEST;

    mdi_node_ref_t root, node;
    uint8_t u8;
    int32_t i32;
    uint32_t u32;
    uint64_t u64;
    bool b;

    EXPECT_EQ(mdi_init(mdi_data, mdi_length, &root), 0, "mdi_init failed");

    // uint8 test
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT8, &node), 0,
              "MDI_TEST_UINT8 not found");
    EXPECT_EQ(mdi_node_uint8(&node, &u8), 0, "mdi_node_uint8 failed");
    EXPECT_EQ(u8, 123, "mdi_node_uint8 returned wrong value");

    // int32 test
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_INT32, &node), 0,
              "MDI_TEST_INT32 not found");
    EXPECT_EQ(mdi_node_int32(&node, &i32), 0, "mdi_node_int32 failed");
    EXPECT_EQ(i32, -123, "mdi_node_int32 returned wrong value");

    // uint32 test
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT32, &node), 0,
              "MDI_TEST_UINT32 not found");
    EXPECT_EQ(mdi_node_uint32(&node, &u32), 0, "mdi_node_uint32 failed");
    EXPECT_EQ(u32, 0xFFFFFFFFu, "mdi_node_uint32 returned wrong value");

    // uint64 test
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT64, &node), 0,
              "MDI_TEST_UINT64 not found");
    EXPECT_EQ(mdi_node_uint64(&node, &u64), 0, "mdi_node_uint64 failed");
    EXPECT_EQ(u64, 0x3FFFFFFFFu, "mdi_node_uint64 returned wrong value");

    // boolean test
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_BOOLEAN_TRUE, &node), 0,
              "MDI_TEST_BOOLEAN_TRUE not found");
    EXPECT_EQ(mdi_node_boolean(&node, &b), 0, "mdi_node_boolean failed");
    EXPECT_EQ(b, true, "mdi_node_boolean returned wrong value");
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_BOOLEAN_FALSE, &node), 0,
              "MDI_TEST_BOOLEAN_FALSE not found");
    EXPECT_EQ(mdi_node_boolean(&node, &b), 0, "mdi_node_boolean failed");
    EXPECT_EQ(b, false, "mdi_node_boolean returned wrong value");

    // string test
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_STRING, &node), 0,
              "MDI_TEST_STRING not found");
    const char* string = mdi_node_string(&node);
    ASSERT_NE(string, NULL, "mdi_node_string returned NULL");
    EXPECT_EQ(strcmp(string, "hello"), 0, "mdi_node_string failed");

    END_TEST;
}

bool array_tests(void) {
    BEGIN_TEST;

    mdi_node_ref_t root, node;

    EXPECT_EQ(mdi_init(mdi_data, mdi_length, &root), 0, "mdi_init failed");

    // test boolean array
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_BOOL_ARRAY, &node), 0,
              "MDI_TEST_BOOL_ARRAY not found");
    EXPECT_EQ(mdi_array_length(&node), 3u, "mdi_array_length failed");
    bool b[4];
    EXPECT_EQ(mdi_array_boolean(&node, 0, &b[0]), 0, "mdi_array_boolean failed");
    EXPECT_EQ(mdi_array_boolean(&node, 1, &b[1]), 0, "mdi_array_boolean failed");
    EXPECT_EQ(mdi_array_boolean(&node, 2, &b[2]), 0, "mdi_array_boolean failed");
    EXPECT_NE(mdi_array_boolean(&node, 3, &b[3]), 0,
              "mdi_array_boolean succeeded for out of range index");
    EXPECT_EQ(b[0], true, "mdi_array_boolean returned wrong value");
    EXPECT_EQ(b[1], false, "mdi_array_boolean returned wrong value");
    EXPECT_EQ(b[2], true, "mdi_array_boolean returned wrong value");

    // test empty array
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_EMPTY_BOOL_ARRAY, &node), 0,
              "MDI_TEST_EMPTY_BOOL_ARRAY not found");
    EXPECT_EQ(mdi_array_length(&node), 0u, "mdi_array_length failed");

    // test uint8 array
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT8_ARRAY, &node), 0,
              "MDI_TEST_UINT8_ARRAY not found");
    EXPECT_EQ(mdi_array_length(&node), 3u, "mdi_array_length failed");
    uint8_t u8[4];
    EXPECT_EQ(mdi_array_uint8(&node, 0, &u8[0]), 0, "mdi_array_uint8 failed");
    EXPECT_EQ(mdi_array_uint8(&node, 1, &u8[1]), 0, "mdi_array_uint8 failed");
    EXPECT_EQ(mdi_array_uint8(&node, 2, &u8[2]), 0, "mdi_array_uint8 failed");
    EXPECT_NE(mdi_array_uint8(&node, 3, &u8[3]), 0,
              "mdi_array_uint8 succeeded for out of range index");
    EXPECT_EQ(u8[0], 1u, "mdi_array_uint8 returned wrong value");
    EXPECT_EQ(u8[1], 2u, "mdi_array_uint8 returned wrong value");
    EXPECT_EQ(u8[2], 3u, "mdi_array_uint8 returned wrong value");

    // test int32 array
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_INT32_ARRAY, &node), 0,
              "MDI_TEST_INT32_ARRAY not found");
    EXPECT_EQ(mdi_array_length(&node), 3u, "mdi_array_length failed");
    int32_t i32[4];
    EXPECT_EQ(mdi_array_int32(&node, 0, &i32[0]), 0, "mdi_array_int32 failed");
    EXPECT_EQ(mdi_array_int32(&node, 1, &i32[1]), 0, "mdi_array_int32 failed");
    EXPECT_EQ(mdi_array_int32(&node, 2, &i32[2]), 0, "mdi_array_int32 failed");
    EXPECT_NE(mdi_array_int32(&node, 3, &i32[3]), 0,
              "mdi_array_int32 succeeded for out of range index");
    EXPECT_EQ(i32[0], -1, "mdi_array_int32 returned wrong value");
    EXPECT_EQ(i32[1], -2, "mdi_array_int32 returned wrong value");
    EXPECT_EQ(i32[2], -3, "mdi_array_int32 returned wrong value");

    // test uint32 array
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT32_ARRAY, &node), 0,
              "MDI_TEST_UINT32_ARRAY not found");
    EXPECT_EQ(mdi_array_length(&node), 3u, "mdi_array_length failed");
    uint32_t u32[4];
    EXPECT_EQ(mdi_array_uint32(&node, 0, &u32[0]), 0, "mdi_array_uint32 failed");
    EXPECT_EQ(mdi_array_uint32(&node, 1, &u32[1]), 0, "mdi_array_uint32 failed");
    EXPECT_EQ(mdi_array_uint32(&node, 2, &u32[2]), 0, "mdi_array_uint32 failed");
    EXPECT_NE(mdi_array_uint32(&node, 3, &u32[3]), 0,
              "mdi_array_uint32 succeeded for out of range index");
    EXPECT_EQ(u32[0], 1u, "mdi_array_uint32 returned wrong value");
    EXPECT_EQ(u32[1], 2u, "mdi_array_uint32 returned wrong value");
    EXPECT_EQ(u32[2], 3u, "mdi_array_uint32 returned wrong value");

    // test uint64 array
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT64_ARRAY, &node), 0,
              "MDI_TEST_UINT64_ARRAY not found");
    EXPECT_EQ(mdi_array_length(&node), 3u, "mdi_array_length failed");
    uint64_t u64[4];
    EXPECT_EQ(mdi_array_uint64(&node, 0, &u64[0]), 0, "mdi_array_uint64 failed");
    EXPECT_EQ(mdi_array_uint64(&node, 1, &u64[1]), 0, "mdi_array_uint64 failed");
    EXPECT_EQ(mdi_array_uint64(&node, 2, &u64[2]), 0, "mdi_array_uint64 failed");
    EXPECT_NE(mdi_array_uint64(&node, 3, &u64[3]), 0,
              "mdi_array_uint64 succeeded for out of range index");
    EXPECT_EQ(u64[0], 0x100000000u, "mdi_array_uint64 returned wrong value");
    EXPECT_EQ(u64[1], 0x200000000u, "mdi_array_uint64 returned wrong value");
    EXPECT_EQ(u64[2], 0x300000000u, "mdi_array_uint64 returned wrong value");

    END_TEST;
}

bool anonymous_list_tests(void) {
    BEGIN_TEST;

    mdi_node_ref_t root, node, child;
    int32_t i32;
    const char* string;

    const int32_t test_ints[] = {
        1, 2, 3
    };
    const char* test_strings[] = {
        "one", "two", "three"
    };

    EXPECT_EQ(mdi_init(mdi_data, mdi_length, &root), 0, "mdi_init failed");

    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_LIST, &node), 0, "MDI_TEST_LIST not found");

    int i = 0;
    mdi_each_child(&node, &child) {
        mdi_node_ref_t grand_child;
        EXPECT_EQ(mdi_first_child(&child, &grand_child), 0, "mdi_first_child failed");
        EXPECT_EQ(mdi_node_type(&grand_child), (uint32_t)MDI_INT32, "expected type MDI_INT32");
        EXPECT_EQ(grand_child.node->id, (uint32_t)MDI_TEST_LIST_INT,
                  "expected MDI_TEST_LIST_ARRAY_INT");
        EXPECT_EQ(mdi_node_int32(&grand_child, &i32), 0, "mdi_array_int32 failed");
        EXPECT_EQ(i32, test_ints[i], "mdi_node_int32 returned wrong value");
        EXPECT_EQ(mdi_next_child(&grand_child, &grand_child), 0, "mdi_next_child failed");
        EXPECT_EQ(mdi_node_type(&grand_child), (uint32_t)MDI_STRING, "expected type MDI_STRING");
        EXPECT_EQ(grand_child.node->id, (uint32_t)MDI_TEST_LIST_STR,
                  "expected MDI_TEST_LIST_ARRAY_STR");
        string = mdi_node_string(&grand_child);
        ASSERT_NE(string, NULL, "mdi_node_string returned NULL");
        EXPECT_EQ(strcmp(string, test_strings[i]), 0, "mdi_node_string failed");
        // should be end of child list
        EXPECT_NE(mdi_next_child(&grand_child, &grand_child), 0,
                  "mdi_next_child shouldn't have succeeded");

        i++;
    }

    EXPECT_EQ(i, 3, "wrong number of iterations through MDI_TEST_LIST");

    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_EMPTY_LIST, &node), 0, "MDI_TEST_EMPTY_LIST not found");
    EXPECT_EQ(mdi_child_count(&node), 0u, "MDI_TEST_EMPTY_LIST not empty");

    END_TEST;
}

bool expression_tests(void) {
    BEGIN_TEST;

    mdi_node_ref_t root, array;

    EXPECT_EQ(mdi_init(mdi_data, mdi_length, &root), 0, "mdi_init failed");

    // uint8_t expressions
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT8_EXPRS, &array), 0,
              "MDI_TEST_UINT8_EXPRS not found");
    uint32_t length = mdi_array_length(&array);
    EXPECT_EQ(length % 2, 0u, "array length not even");
    for (uint32_t i = 0; i < length; ) {
        uint8_t x, y;
        EXPECT_EQ(mdi_array_uint8(&array, i++, &x), 0, "mdi_array_uint8 failed");
        EXPECT_EQ(mdi_array_uint8(&array, i++, &y), 0, "mdi_array_uint8 failed");
        EXPECT_EQ(x, y, "values not equal in uint8-exprs");
    }

    // int32_t expressions
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_INT32_EXPRS, &array), 0,
              "MDI_TEST_INT32_EXPRS not found");
    length = mdi_array_length(&array);
    EXPECT_EQ(length % 2, 0u, "array length not even");
    for (uint32_t i = 0; i < length; ) {
        int32_t x, y;
        EXPECT_EQ(mdi_array_int32(&array, i++, &x), 0, "mdi_array_int32 failed");
        EXPECT_EQ(mdi_array_int32(&array, i++, &y), 0, "mdi_array_int32 failed");
        EXPECT_EQ(x, y, "values not equal in int32-exprs");
    }

    // uint32_t expressions
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT32_EXPRS, &array), 0,
              "MDI_TEST_UINT32_EXPRS not found");
    length = mdi_array_length(&array);
    EXPECT_EQ(length % 2, 0u, "array length not even");
    for (uint32_t i = 0; i < length; ) {
        uint32_t x, y;
        EXPECT_EQ(mdi_array_uint32(&array, i++, &x), 0, "mdi_array_uint32 failed");
        EXPECT_EQ(mdi_array_uint32(&array, i++, &y), 0, "mdi_array_uint32 failed");
        EXPECT_EQ(x, y, "values not equal in uint32-exprs");
    }

    // uint64_t expressions
    EXPECT_EQ(mdi_find_node(&root, MDI_TEST_UINT64_EXPRS, &array), 0,
              "MDI_TEST_UINT64_EXPRS not found");
    length = mdi_array_length(&array);
    EXPECT_EQ(length % 2, 0u, "array length not even");
    for (uint32_t i = 0; i < length; ) {
        uint64_t x, y;
        EXPECT_EQ(mdi_array_uint64(&array, i++, &x), 0, "mdi_array_uint64 failed");
        EXPECT_EQ(mdi_array_uint64(&array, i++, &y), 0, "mdi_array_uint64 failed");
        EXPECT_EQ(x, y, "values not equal in uint64-exprs");
    }

    END_TEST;
}

BEGIN_TEST_CASE(mdi_tests)
RUN_TEST(load_mdi);
RUN_TEST(simple_tests);
RUN_TEST(array_tests);
RUN_TEST(anonymous_list_tests);
RUN_TEST(expression_tests);
END_TEST_CASE(mdi_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

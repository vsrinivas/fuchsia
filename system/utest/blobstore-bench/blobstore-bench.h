// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

constexpr size_t B = (1);
constexpr size_t KB = (1 << 10);
constexpr size_t MB = (1 << 20);

typedef enum {
    DEFAULT, // forward (default) order
    REVERSE, // reverse order
    RANDOM, // random order
    FIRST, // first 100
    LAST, // last 100
    ORDER_COUNT, // number of order options
} traversal_order_t;

typedef enum {
    CREATE, // create blob
    TRUNCATE, // truncate blob
    WRITE, // write data to blob
    OPEN, // open fd to blob
    READ, // read data from blob
    CLOSE, // close blob fd
    UNLINK, // unlink blob
    NAME_COUNT // number of name options
} test_name_t;

// An in-memory representation of a blob.
typedef struct blob_info {
    char path[PATH_MAX];
    fbl::unique_ptr<char[]> merkle;
    size_t size_merkle;
    fbl::unique_ptr<char[]> data;
    size_t size_data;
} blob_info_t;

class TestData {
public:
    TestData(size_t blob_size, size_t blob_count, traversal_order_t order);
    ~TestData();
    bool run_tests();
private:
    // setup
    void generate_order();
    size_t get_max_count();
    void get_name_str(test_name_t name, char* name_str);
    void get_order_str(char* order_str);
    void print_order();

    // reporting
    inline void sample_end(mx_time_t start, test_name_t name, size_t index);
    bool report_test(test_name_t name);

    // tests
    bool create_blobs();
    bool read_blobs();
    bool unlink_blobs();

    // state
    size_t blob_size;
    size_t blob_count;
    traversal_order_t order;
    size_t* indices;
    mx_time_t** samples;
    char** paths;
};

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <limits.h>
#include <stdlib.h>
#include <zircon/types.h>

enum class TraversalOrder : int {
    kDefault, // forward (default) order
    kReverse, // reverse order
    kRandom,  // random order
    kFirst,   // first 100
    kLast,    // last 100
    kCount,   // number of order options
};

enum class TestName : int {
    kCreate,   // create blob
    kTruncate, // truncate blob
    kWrite,    // write data to blob
    kOpen,     // open fd to blob
    kRead,     // read data from blob
    kClose,    // close blob fd
    kUnlink,   // unlink blob
    kCount,    // number of name options
};

// An in-memory representation of a blob.
struct BlobInfo {
    char path[PATH_MAX];
    fbl::unique_ptr<char[]> merkle;
    size_t size_merkle;
    fbl::unique_ptr<char[]> data;
    size_t size_data;
};

class TestData {
public:
    TestData(size_t blob_size, size_t blob_count, TraversalOrder order);
    ~TestData();
    bool RunTests();

private:
    // setup
    void GenerateOrder();
    size_t GetMaxCount();
    void GetNameStr(TestName name, char* name_str);
    void GetOrderStr(char* order_str);
    void PrintOrder();

    // reporting
    inline void SampleEnd(zx_time_t start, TestName name, size_t index);
    bool ReportTest(TestName name);

    // tests
    bool CreateBlobs();
    bool ReadBlobs();
    bool UnlinkBlobs();
    bool Sync();

    // state
    size_t* indices_;
    zx_time_t** samples_;
    char** paths_;
    size_t blob_size_;
    size_t blob_count_;
    TraversalOrder order_;
};

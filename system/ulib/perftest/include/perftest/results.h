// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <fbl/string.h>
#include <fbl/vector.h>

// This is a convenience library for outputting the raw data produced by a
// performance test in JSON format.  This allows reporting the time taken
// by each run of a test case, for example.
//
// This uses the JSON output format described in the Fuchsia Tracing
// Usage Guide:
// https://fuchsia.googlesource.com/garnet/+/master/docs/tracing_usage_guide.md#benchmark-result-export
//
// Having a library for this should allow us to more easily change the JSON
// output format while ensuring that various performance tests are updated
// to produce the current version of the output format.

namespace perftest {

void WriteJSONString(FILE* out_file, const char* string);

struct SummaryStatistics {
    double min;
    double max;
    double mean;
    double std_dev;
};

// This represents the results for a particular test case.  It contains a
// sequence of values, which are typically the times taken by each run of
// the test case, in order.
class TestCaseResults {
public:
    TestCaseResults(const fbl::String& label, const fbl::String& unit)
        : label_(label),
          unit_(unit) {}

    fbl::String label() const { return label_; }
    fbl::String unit() const { return unit_; }
    fbl::Vector<double>* values() { return &values_; }

    void AppendValue(double value) {
        values_.push_back(value);
    }

    // A caller may check for errors using ferror().
    void WriteJSON(FILE* out_file) const;

    SummaryStatistics GetSummaryStatistics() const;

private:
    fbl::String label_;
    fbl::String unit_;
    fbl::Vector<double> values_;
};

// This represents the results for a set of test cases.
//
// The test cases may be kept in the order in which they were run, in case
// ordering is significant.  (For example, it might turn out that one test
// case affects a later test case.)
class ResultsSet {
public:
    fbl::Vector<TestCaseResults>* results() { return &results_; }

    TestCaseResults* AddTestCase(const fbl::String& label,
                                 const fbl::String& unit);

    // A caller may check for errors using ferror().
    void WriteJSON(FILE* out_file) const;
    void PrintSummaryStatistics(FILE* out_file) const;

private:
    fbl::Vector<TestCaseResults> results_;
};

} // namespace perftest

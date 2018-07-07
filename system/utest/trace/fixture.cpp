// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixture.h"

#include <regex.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <zircon/assert.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/event.h>
#include <trace-reader/reader.h>
#include <trace/handler.h>
#include <unittest/unittest.h>

namespace {

class Fixture : private trace::TraceHandler {
public:
    Fixture(trace_buffering_mode_t mode, size_t buffer_size)
        : loop_(&kAsyncLoopConfigNoAttachToThread),
          buffering_mode_(mode),
          buffer_(new uint8_t[buffer_size], buffer_size) {
        zx_status_t status = zx::event::create(0u, &trace_stopped_);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }

    ~Fixture() {
        StopTracing(false);
    }

    void StartTracing() {
        if (trace_running_)
            return;

        trace_running_ = true;
        loop_.StartThread("trace test");

        // Asynchronously start the engine.
        zx_status_t status = trace_start_engine(loop_.dispatcher(), this,
                                                buffering_mode_,
                                                buffer_.get(), buffer_.size());
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }

    void StopTracing(bool hard_shutdown) {
        if (!trace_running_)
            return;

        // Asynchronously stop the engine.
        // If we're performing a hard shutdown, skip this step and begin immediately
        // tearing down the loop.  The trace engine should stop itself.
        if (!hard_shutdown) {
            zx_status_t status = trace_stop_engine(ZX_OK);
            ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);

            status = trace_stopped_.wait_one(ZX_EVENT_SIGNALED,
                                             zx::deadline_after(zx::msec(1000)), nullptr);
            ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);
        }

        // Shut down the loop (implicily joins the thread we started earlier).
        // When this completes we know the trace engine is really stopped.
        loop_.Shutdown();

        ZX_DEBUG_ASSERT(observed_stopped_callback_);

        trace_running_ = false;
    }

    zx_status_t disposition() const {
        return disposition_;
    }

    bool ReadRecords(fbl::Vector<trace::Record>* out_records,
                     fbl::Vector<fbl::String>* out_errors) {
        trace::TraceReader reader(
            [out_records](trace::Record record) {
                out_records->push_back(fbl::move(record));
            },
            [out_errors](fbl::String error) {
                out_errors->push_back(fbl::move(error));
            });
        trace::Chunk chunk(reinterpret_cast<uint64_t*>(buffer_.get()),
                           buffer_bytes_written_ / 8u);
        if (buffer_bytes_written_ & 7u) {
            out_errors->push_back(fbl::String("Buffer contains extraneous bytes"));
        }
        if (!reader.ReadRecords(chunk)) {
            out_errors->push_back(fbl::String("Trace data is corrupted"));
        }
        return out_errors->is_empty();
    }

private:
    bool IsCategoryEnabled(const char* category) override {
        // All categories which begin with + are enabled.
        return category[0] == '+';
    }

    void TraceStopped(async_dispatcher_t* dispatcher,
                      zx_status_t disposition,
                      size_t buffer_bytes_written) override {
        ZX_DEBUG_ASSERT(!observed_stopped_callback_);
        observed_stopped_callback_ = true;
        ZX_DEBUG_ASSERT(dispatcher = loop_.dispatcher());
        disposition_ = disposition;
        buffer_bytes_written_ = buffer_bytes_written;

        trace_stopped_.signal(0u, ZX_EVENT_SIGNALED);

        // The normal provider support does "delete this" here.
        // We don't need nor want it as we still have to verify the results.
    }

    async::Loop loop_;
    trace_buffering_mode_t buffering_mode_;
    fbl::Array<uint8_t> buffer_;
    bool trace_running_ = false;
    zx_status_t disposition_ = ZX_ERR_INTERNAL;
    size_t buffer_bytes_written_ = 0u;
    zx::event trace_stopped_;
    bool observed_stopped_callback_ = false;
};

Fixture* g_fixture{nullptr};

} // namespace

struct FixtureSquelch {
    // Records the compiled regex.
    regex_t regex;
};

void fixture_set_up(trace_buffering_mode_t mode, size_t buffer_size) {
    ZX_DEBUG_ASSERT(!g_fixture);
    g_fixture = new Fixture(mode, buffer_size);
}

void fixture_tear_down(void) {
    ZX_DEBUG_ASSERT(g_fixture);
    delete g_fixture;
    g_fixture = nullptr;
}

void fixture_start_tracing() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StartTracing();
}

void fixture_stop_tracing() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopTracing(false);
}

void fixture_stop_tracing_hard() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopTracing(true);
}

zx_status_t fixture_get_disposition(void) {
    ZX_DEBUG_ASSERT(g_fixture);
    return g_fixture->disposition();
}

bool fixture_create_squelch(const char* regex_str, FixtureSquelch** out_squelch) {
    // We don't make any assumptions about the copyability of |regex_t|.
    // Therefore we construct it in place.
    auto squelch = new FixtureSquelch;
    if (regcomp(&squelch->regex, regex_str, REG_EXTENDED | REG_NEWLINE) != 0) {
        return false;
    }
    *out_squelch = squelch;
    return true;
}

void fixture_destroy_squelch(FixtureSquelch* squelch) {
    regfree(&squelch->regex);
    delete squelch;
}

fbl::String fixture_squelch(FixtureSquelch* squelch, const char* str) {
    fbl::StringBuffer<1024u> buf;
    const char* cur = str;
    const char* end = str + strlen(str);
    while (*cur) {
        // size must be 1 + number of parenthesized subexpressions
        size_t match_count = squelch->regex.re_nsub + 1;
        regmatch_t match[match_count];
        if (regexec(&squelch->regex, cur, match_count, match, 0) != 0) {
            buf.Append(cur, end - cur);
            break;
        }
        size_t offset = 0u;
        for (size_t i = 1; i < match_count; i++) {
            if (match[i].rm_so == -1)
                continue;
            buf.Append(cur, match[i].rm_so - offset);
            buf.Append("<>");
            cur += match[i].rm_eo - offset;
            offset = match[i].rm_eo;
        }
    }
    return buf;
}

bool fixture_compare_raw_records(const fbl::Vector<trace::Record>& records,
                                 size_t start_record, size_t max_num_records,
                                 const char* expected) {
    BEGIN_HELPER;

    // Append |num_records| records to the buffer, replacing each match of a parenthesized
    // subexpression of the regex with "<>".  This is used to strip out timestamps
    // and other varying data that is not controlled by these tests.
    FixtureSquelch* squelch;
    ASSERT_TRUE(fixture_create_squelch(
                    "([0-9]+/[0-9]+)"
                    "|koid\\(([0-9]+)\\)"
                    "|koid: ([0-9]+)"
                    "|ts: ([0-9]+)"
                    "|(0x[0-9a-f]+)",
                    &squelch), "error creating squelch");

    fbl::StringBuffer<16384u> buf;
    size_t num_recs = 0;
    for (size_t i = start_record; i < records.size(); ++i) {
        if (num_recs == max_num_records)
            break;
        const auto& record = records[i];
        fbl::String str = record.ToString();
        buf.Append(fixture_squelch(squelch, str.c_str()));
        buf.Append('\n');
        ++num_recs;
    }
    EXPECT_STR_EQ(expected, buf.c_str(), "unequal cstr");
    fixture_destroy_squelch(squelch);

    END_HELPER;
}

bool fixture_compare_n_records(size_t max_num_records, const char* expected,
                               fbl::Vector<trace::Record>* out_records) {
    ZX_DEBUG_ASSERT(g_fixture);
    BEGIN_HELPER;

    g_fixture->StopTracing(false);

    fbl::Vector<trace::Record> records;
    fbl::Vector<fbl::String> errors;
    EXPECT_TRUE(g_fixture->ReadRecords(&records, &errors), "read error");

    for (const auto& error : errors)
        printf("error: %s\n", error.c_str());
    ASSERT_EQ(0u, errors.size(), "errors encountered");

    ASSERT_GE(records.size(), 1u, "expected an initialization record");
    ASSERT_EQ(trace::RecordType::kInitialization, records[0].type(),
              "expected initialization record");
    EXPECT_EQ(zx_ticks_per_second(),
              records[0].GetInitialization().ticks_per_second);
    records.erase(0);

    EXPECT_TRUE(fixture_compare_raw_records(records, 0, max_num_records, expected));

    if (out_records) {
        *out_records = fbl::move(records);
    }

    END_HELPER;
}

bool fixture_compare_records(const char* expected) {
    return fixture_compare_n_records(SIZE_MAX, expected, nullptr);
}

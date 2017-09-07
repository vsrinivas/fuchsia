// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixture.h"

#include <regex.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <magenta/assert.h>

#include <async/loop.h>
#include <mx/event.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <trace-reader/reader.h>
#include <trace/handler.h>
#include <unittest/unittest.h>

namespace {

static constexpr size_t kBufferSizeBytes = 1024 * 1024;

class Fixture : private trace::TraceHandler {
public:
    Fixture()
        : buffer_(new uint8_t[kBufferSizeBytes], kBufferSizeBytes) {
        mx_status_t status = mx::event::create(0u, &trace_stopped_);
        MX_DEBUG_ASSERT(status == MX_OK);
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
        mx_status_t status = trace_start_engine(loop_.async(), this,
                                                buffer_.get(), buffer_.size());
        MX_DEBUG_ASSERT(status == MX_OK);
    }

    void StopTracing(bool hard_shutdown) {
        if (!trace_running_)
            return;

        // Asynchronously stop the engine.
        // If we're performing a hard shutdown, skip this step and begin immediately
        // tearing down the loop.  The trace engine should stop itself.
        if (!hard_shutdown) {
            mx_status_t status = trace_stop_engine(MX_OK);
            MX_DEBUG_ASSERT(status == MX_OK);

            status = trace_stopped_.wait_one(MX_EVENT_SIGNALED,
                                             mx_deadline_after(MX_MSEC(1000)), nullptr);
            MX_DEBUG_ASSERT(status == MX_OK);
        }

        // Shut down the loop (implicily joins the thread we started earlier).
        // When this completes we know the trace engine is really stopped.
        loop_.Shutdown();

        MX_DEBUG_ASSERT(observed_stopped_callback_);

        trace_running_ = false;
    }

    mx_status_t disposition() const {
        return disposition_;
    }

    bool ReadRecords(fbl::Vector<trace::Record>* out_records,
                     fbl::Vector<fbl::String>* out_errors) {
        trace::TraceReader reader(
            [out_records](trace::Record record) { out_records->push_back(fbl::move(record)); },
            [out_errors](fbl::String error) { out_errors->push_back(fbl::move(error)); });
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

    void TraceStopped(async_t* async,
                      mx_status_t disposition,
                      size_t buffer_bytes_written) override {
        MX_DEBUG_ASSERT(!observed_stopped_callback_);
        observed_stopped_callback_ = true;
        MX_DEBUG_ASSERT(async = loop_.async());
        disposition_ = disposition;
        buffer_bytes_written_ = buffer_bytes_written;

        trace_stopped_.signal(0u, MX_EVENT_SIGNALED);
    }

    async::Loop loop_;
    fbl::Array<uint8_t> buffer_;
    bool trace_running_ = false;
    mx_status_t disposition_ = MX_ERR_INTERNAL;
    size_t buffer_bytes_written_ = 0u;
    mx::event trace_stopped_;
    bool observed_stopped_callback_ = false;
};

Fixture* g_fixture{nullptr};

} // namespace

void fixture_set_up(void) {
    MX_DEBUG_ASSERT(!g_fixture);
    g_fixture = new Fixture();
}

void fixture_tear_down(void) {
    MX_DEBUG_ASSERT(g_fixture);
    delete g_fixture;
    g_fixture = nullptr;
}

void fixture_start_tracing() {
    MX_DEBUG_ASSERT(g_fixture);
    g_fixture->StartTracing();
}

void fixture_stop_tracing() {
    MX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopTracing(false);
}

void fixture_stop_tracing_hard() {
    MX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopTracing(true);
}

mx_status_t fixture_get_disposition(void) {
    MX_DEBUG_ASSERT(g_fixture);
    return g_fixture->disposition();
}

bool fixture_compare_records(const char* expected) {
    MX_DEBUG_ASSERT(g_fixture);
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
    EXPECT_EQ(mx_ticks_per_second(),
              records[0].GetInitialization().ticks_per_second);
    records.erase(0);

    // Append all records to the buffer, replacing each match of a parenthesized
    // subexpression of the regex with "<>".  This is used to strip out timestamps
    // and other varying data that is not controlled by these tests.
    regex_t regex;
    ASSERT_EQ(0, regcomp(&regex,
                         "([0-9]+/[0-9]+)"
                         "|koid\\(([0-9]+)\\)"
                         "|koid: ([0-9]+)"
                         "|ts: ([0-9]+)"
                         "|(0x[0-9a-f]+)",
                         REG_EXTENDED | REG_NEWLINE));

    fbl::StringBuffer<16384u> buf;
    for (const auto& record : records) {
        fbl::String str = record.ToString();
        const char* cur = str.c_str();
        while (*cur) {
            regmatch_t match[6]; // count must be 1 + number of parenthesized subexpressions
            if (regexec(&regex, cur, fbl::count_of(match), match, 0) != 0) {
                buf.Append(cur, str.end() - cur);
                break;
            }
            size_t offset = 0u;
            for (size_t i = 1; i < fbl::count_of(match); i++) {
                if (match[i].rm_so == -1)
                    continue;
                buf.Append(cur, match[i].rm_so - offset);
                buf.Append("<>");
                cur += match[i].rm_eo - offset;
                offset = match[i].rm_eo;
            }
        }
        buf.Append('\n');
    }
    EXPECT_STR_EQ(expected, buf.c_str(), strlen(expected) + 1u, "unequal cstr");
    regfree(&regex);

    END_HELPER;
}

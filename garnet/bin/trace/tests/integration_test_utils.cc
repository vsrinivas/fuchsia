// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <src/lib/fxl/logging.h>
#include <lib/zx/time.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <trace/event.h>
#include <trace/observer.h>
#include <zircon/status.h>

#include "garnet/bin/trace/spec.h"
#include "garnet/bin/trace/tests/integration_test_utils.h"

// The name of the trace events member in the json output file.
const char kTraceEventsMemberName[] = "traceEvents";

// The name of the category member in the json output file.
const char kCategoryMemberName[] = "cat";

// The name of the event name member in the json output file.
const char kEventNameMemberName[] = "name";

// Category for events we generate.
#define CATEGORY_NAME "trace:test"

// Name to use in instant events.
#define INSTANT_EVENT_NAME "instant"

// Size in bytes of the records |WriteTestRecords()| emits.
// We assume strings and thread references are not inlined. If they are that's
// ok. The point is this value is the minimum size of the record we're going to
// emit. If the record is larger then the trace will be larger, which is ok.
// If it's smaller we risk not stress-testing things enough.
// header-word(8) + ticks(8) + 3 arguments (= 3 * (8 + 8)) = 64
constexpr size_t kRecordSize = 64;

bool CreateProviderSynchronously(
    async::Loop& loop, const char* name,
    fbl::unique_ptr<trace::TraceProvider>* out_provider) {
  async_dispatcher_t* dispatcher = loop.dispatcher();

  fbl::unique_ptr<trace::TraceProvider> provider;
  bool already_started;
  if (!trace::TraceProvider::CreateSynchronously(dispatcher, name, &provider,
                                                 &already_started)) {
    FXL_LOG(ERROR) << "Failed to create provider " << name;
    return false;
  }

  if (already_started) {
    // At this point we're registered with trace-manager, and we know tracing
    // has started. But we haven't received the Start() request yet, which
    // contains the trace buffer (as a vmo) and other things. So wait for it.
    if (!WaitForTracingToStart(loop, kStartTimeout)) {
      FXL_LOG(ERROR) << "Provider " << name
                     << " failed waiting for tracing to start";
      return false;
    }
  }

  *out_provider = std::move(provider);
  return true;
}

void WriteTestEvents(size_t num_records) {
  for (size_t i = 0; i < num_records; ++i) {
    TRACE_INSTANT(CATEGORY_NAME, INSTANT_EVENT_NAME, TRACE_SCOPE_PROCESS,
                  "arg1", 1, "arg2", 2, "arg3", 3);
  }
}

bool VerifyTestEvents(const std::string& test_output_file,
                      size_t* out_num_events) {
  // We don't know how many records got dropped, but we can count them,
  // and verify they are what we expect.
  std::ifstream in(test_output_file);
  rapidjson::IStreamWrapper isw(in);
  rapidjson::Document document;

  if (!document.ParseStream(isw).IsObject()) {
    FXL_LOG(ERROR) << "Failed to parse JSON object from: " << test_output_file;
    if (document.HasParseError()) {
      FXL_LOG(ERROR) << "Parse error "
                     << GetParseError_En(document.GetParseError()) << " ("
                     << document.GetErrorOffset() << ")";
    }
    return false;
  }

  auto events_it = document.FindMember(kTraceEventsMemberName);
  if (events_it == document.MemberEnd()) {
    FXL_LOG(ERROR) << "Member not found: " << kTraceEventsMemberName;
    return false;
  }
  const auto& value = events_it->value;
  if (!value.IsArray()) {
    FXL_LOG(ERROR) << kTraceEventsMemberName << " is not an array";
    return false;
  }

  const auto& array = value.GetArray();
  for (size_t i = 0; i < array.Size(); ++i) {
    if (!array[i].IsObject()) {
      FXL_LOG(ERROR) << "Event " << i << " is not an object";
      return false;
    }

    const auto& event = array[i];
    auto cat_it = event.FindMember(kCategoryMemberName);
    if (cat_it == event.MemberEnd()) {
      FXL_LOG(ERROR) << "Category not present in event";
      return false;
    }
    const auto& category_name = cat_it->value;
    if (!category_name.IsString()) {
      FXL_LOG(ERROR) << "Category name is not a string";
      return false;
    }
    if (strcmp(category_name.GetString(), CATEGORY_NAME) != 0) {
      FXL_LOG(ERROR) << "Expected category not present in event, got: "
                     << category_name.GetString();
      return false;
    }

    auto name_it = event.FindMember(kEventNameMemberName);
    if (name_it == event.MemberEnd()) {
      FXL_LOG(ERROR) << "Event name not present in event";
      return false;
    }
    const auto& event_name = name_it->value;
    if (!event_name.IsString()) {
      FXL_LOG(ERROR) << "Event name is not a string";
      return false;
    }
    if (strcmp(event_name.GetString(), INSTANT_EVENT_NAME) != 0) {
      FXL_LOG(ERROR) << "Expected event not present in event, got: "
                     << event_name.GetString();
      return false;
    }
  }

  FXL_VLOG(1) << array.Size() << " trace events present";
  *out_num_events = array.Size();
  return true;
}

void FillBuffer(size_t num_times, size_t buffer_size_in_mb) {
  FXL_DCHECK(num_times && buffer_size_in_mb);
  size_t buffer_size = buffer_size_in_mb * 1024 * 1024;
  size_t num_iterations = buffer_size / kRecordSize;

  for (size_t i = 0; i < num_times; ++i) {
    if (i > 0) {
      // The buffer is roughly full at this point.
      // Give TraceManager some time to catch up in streaming mode
      // (but not too much time).
      zx::nanosleep(zx::deadline_after(zx::sec(1)));
    }
    WriteTestEvents(num_iterations);
  }
}

static size_t GetMinimumNumberOfEvents(tracing::BufferingMode buffering_mode,
                                       size_t buffer_size_in_mb) {
  size_t buffer_size = buffer_size_in_mb * 1024 * 1024;

  // Being hyperaccurate here involves encoding a lot of internal knowledge
  // about how records are stored. Things are also tricky because:
  // - The physical buffer is split up into three pieces in streaming and
  //   circular modes (durable + 2 * rolling). Plus there's the header.
  // - Events go into the rolling buffers, not the durable buffer, and we'd
  //   rather not encode knowlege of their different sizes here. We can be
  //   assured though that the durable buffer size is not greater than the
  //   rolling buffer sizes.
  // - In circular mode it's possible one of the rolling buffers is empty.
  // We just need a lower bound on the number of records that are present.
  double percentage_buffer_filled;
  switch (buffering_mode) {
    case tracing::BufferingMode::kOneshot:
      percentage_buffer_filled = 0.8;
      break;
    case tracing::BufferingMode::kCircular:
      // One of the rolling buffers could be empty.
      // If we conservatively assume durable,rolling buffers are all the same
      // size this could be 0.333. Rounded down to 0.2 as a safe lower bound.
      percentage_buffer_filled = 0.2;
      break;
    case tracing::BufferingMode::kStreaming:
      // If we conservatively assume durable,rolling buffers are all the same
      // size this could be 0.666. Rounded down to 0.5 as a safe lower bound.
      percentage_buffer_filled = 0.5;
      break;
    default:
      FXL_NOTREACHED();
  }

  return (buffer_size / kRecordSize) * percentage_buffer_filled;
}

bool VerifyFullBuffer(const std::string& test_output_file,
                      tracing::BufferingMode buffering_mode,
                      size_t buffer_size_in_mb) {
  size_t num_events;
  if (!VerifyTestEvents(test_output_file, &num_events)) {
    return false;
  }

  size_t min_num_events =
      GetMinimumNumberOfEvents(buffering_mode, buffer_size_in_mb);
  if (num_events < min_num_events) {
    FXL_LOG(ERROR) << "Insufficient number of events present, got "
                   << num_events << ", expected at least " << min_num_events;
    return false;
  }

  return true;
}

bool WaitForTracingToStart(async::Loop& loop, zx::duration timeout) {
  trace::TraceObserver trace_observer;

  bool started = false;
  auto on_trace_state_changed = [&loop, &started]() {
    if (trace_state() == TRACE_STARTED) {
      started = true;
    }
    // Any state change is relevant to us. If we're not started then we must
    // have transitioned from STOPPED to STARTED to at least STOPPING.
    loop.Quit();
  };

  trace_observer.Start(loop.dispatcher(), std::move(on_trace_state_changed));
  if (trace_state() == TRACE_STARTED) {
    return true;
  }

  async::TaskClosure timeout_task([&loop] {
    FXL_LOG(ERROR) << "Timed out waiting for tracing to start";
    loop.Quit();
  });
  timeout_task.PostDelayed(loop.dispatcher(), timeout);
  loop.Run();

  return started;
}

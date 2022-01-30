// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tests/integration_test_utils.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/trace/observer.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <fstream>
#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <trace-reader/file_reader.h>

#include "garnet/bin/trace/options.h"

namespace tracing {
namespace test {

// The name of the trace events member in the json output file.
const char kTraceEventsMemberName[] = "traceEvents";

// The name of the category member in the json output file.
const char kCategoryMemberName[] = "cat";

// The name of the event name member in the json output file.
const char kEventNameMemberName[] = "name";

// Size in bytes of the records |WriteTestEvents()| emits.
// We assume strings and thread references are not inlined. If they are that's
// ok. The point is this value is the minimum size of the record we're going to
// emit. If the record is larger then the trace will be larger, which is ok.
// If it's smaller we risk not stress-testing things enough.
// header-word(8) + ticks(8) + 3 arguments (= 3 * (8 + 8)) = 64
constexpr size_t kRecordSize = 64;

#if USE_STATIC_ENGINE
static zx::channel GetProviderChannel() {
  zx::channel local_endpoint, remote_endpoint;
  zx_status_t status = zx::channel::create(0, &local_endpoint, &remote_endpoint);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create channels " << status;
    return zx::channel();
  }
  status =
      fdio_service_connect("/svc/fuchsia.tracing.provider.Registry", remote_endpoint.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to provider " << status;
    return zx::channel();
  }
  return local_endpoint;
}

bool CreateProviderSynchronously(async::Loop& loop, const char* name,
                                 std::unique_ptr<trace::TraceProvider>* out_provider,
                                 bool* out_already_started) {
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel provider_channel = GetProviderChannel();
  if (!provider_channel)
    return false;

  std::unique_ptr<trace::TraceProvider> provider;
  bool already_started;
  if (!trace::TraceProvider::CreateSynchronously(std::move(provider_channel), dispatcher, name,
                                                 &provider, &already_started)) {
    FX_LOGS(ERROR) << "Failed to create provider " << name;
    return false;
  }

  *out_provider = std::move(provider);
  *out_already_started = already_started;
  return true;
}

bool CreateProviderSynchronouslyAndWait(async::Loop& loop, const char* name,
                                        std::unique_ptr<trace::TraceProvider>* out_provider) {
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel provider_channel = GetProviderChannel();
  if (!provider_channel)
    return false;

  std::unique_ptr<trace::TraceProvider> provider;
  bool already_started;
  if (!trace::TraceProvider::CreateSynchronously(std::move(provider_channel), dispatcher, name,
                                                 &provider, &already_started)) {
    FX_LOGS(ERROR) << "Failed to create provider " << name;
    return false;
  }

  // The program may not be being run under tracing. If it is tracing should have already started.
  if (already_started) {
    // At this point we're registered with trace-manager, and we know tracing
    // has started. But we haven't received the Start() request yet, which
    // contains the trace buffer (as a vmo) and other things. So wait for it.
    if (!WaitForTracingToStart(loop, kStartTimeout)) {
      FX_LOGS(ERROR) << "Provider " << name << " timed out waiting for tracing to start";
      return false;
    }
  }

  *out_provider = std::move(provider);
  return true;
}
#else
bool CreateProviderSynchronously(async::Loop& loop, const char* name,
                                 std::unique_ptr<trace::TraceProvider>* out_provider,
                                 bool* out_already_started) {
  async_dispatcher_t* dispatcher = loop.dispatcher();

  std::unique_ptr<trace::TraceProviderWithFdio> provider;
  bool already_started;
  if (!trace::TraceProviderWithFdio::CreateSynchronously(dispatcher, name, &provider,
                                                         &already_started)) {
    FX_LOGS(ERROR) << "Failed to create provider " << name;
    return false;
  }

  *out_provider = std::move(provider);
  *out_already_started = already_started;
  return true;
}

bool CreateProviderSynchronouslyAndWait(async::Loop& loop, const char* name,
                                        std::unique_ptr<trace::TraceProvider>* out_provider) {
  async_dispatcher_t* dispatcher = loop.dispatcher();

  std::unique_ptr<trace::TraceProviderWithFdio> provider;
  bool already_started;
  if (!trace::TraceProviderWithFdio::CreateSynchronously(dispatcher, name, &provider,
                                                         &already_started)) {
    FX_LOGS(ERROR) << "Failed to create provider " << name;
    return false;
  }

  // The program may not be being run under tracing. If it is tracing should have already started.
  if (already_started) {
    // At this point we're registered with trace-manager, and we know tracing
    // has started. But we haven't received the Start() request yet, which
    // contains the trace buffer (as a vmo) and other things. So wait for it.
    if (!WaitForTracingToStart(loop, kStartTimeout)) {
      FX_LOGS(ERROR) << "Provider " << name << " timed out waiting for tracing to start";
      return false;
    }
  }

  *out_provider = std::move(provider);
  return true;
}
#endif

void WriteTestEvents(size_t num_records) {
  for (size_t i = 0; i < num_records; ++i) {
    TRACE_INSTANT(kWriteTestEventsCategoryName, kWriteTestEventsInstantEventName,
                  TRACE_SCOPE_PROCESS, "arg1", 1, "arg2", 2, "arg3", 3);
  }
}

bool IsWriteTestEvent(const trace::Record& record) {
  if (record.type() == trace::RecordType::kEvent) {
    const trace::Record::Event& event = record.GetEvent();
    if (event.type() == trace::EventType::kInstant &&
        event.category == kWriteTestEventsCategoryName &&
        event.name == kWriteTestEventsInstantEventName) {
      return true;
    }
  }
  return false;
}

bool VerifyTestEventsFromJson(const std::string& test_output_file, size_t* out_num_events) {
  // We don't know how many records got dropped, but we can count them,
  // and verify they are what we expect.
  std::ifstream in(test_output_file);
  rapidjson::IStreamWrapper isw(in);
  rapidjson::Document document;

  if (!document.ParseStream(isw).IsObject()) {
    FX_LOGS(ERROR) << "Failed to parse JSON object from: " << test_output_file;
    if (document.HasParseError()) {
      FX_LOGS(ERROR) << "Parse error " << GetParseError_En(document.GetParseError()) << " ("
                     << document.GetErrorOffset() << ")";
    }
    return false;
  }

  auto events_it = document.FindMember(kTraceEventsMemberName);
  if (events_it == document.MemberEnd()) {
    FX_LOGS(ERROR) << "Member not found: " << kTraceEventsMemberName;
    return false;
  }
  const auto& value = events_it->value;
  if (!value.IsArray()) {
    FX_LOGS(ERROR) << kTraceEventsMemberName << " is not an array";
    return false;
  }

  const auto& array = value.GetArray();
  for (unsigned int i = 0; i < array.Size(); ++i) {
    if (!array[i].IsObject()) {
      FX_LOGS(ERROR) << "Event " << i << " is not an object";
      return false;
    }

    const auto& event = array[i];
    auto cat_it = event.FindMember(kCategoryMemberName);
    if (cat_it == event.MemberEnd()) {
      FX_LOGS(ERROR) << "Category not present in event";
      return false;
    }
    const auto& category_name = cat_it->value;
    if (!category_name.IsString()) {
      FX_LOGS(ERROR) << "Category name is not a string";
      return false;
    }
    if (strcmp(category_name.GetString(), kWriteTestEventsCategoryName) != 0) {
      FX_LOGS(ERROR) << "Expected category not present in event, got: "
                     << category_name.GetString();
      return false;
    }

    auto name_it = event.FindMember(kEventNameMemberName);
    if (name_it == event.MemberEnd()) {
      FX_LOGS(ERROR) << "Event name not present in event";
      return false;
    }
    const auto& event_name = name_it->value;
    if (!event_name.IsString()) {
      FX_LOGS(ERROR) << "Event name is not a string";
      return false;
    }
    if (strcmp(event_name.GetString(), kWriteTestEventsInstantEventName) != 0) {
      FX_LOGS(ERROR) << "Expected event not present in event, got: " << event_name.GetString();
      return false;
    }
  }

  FX_VLOGS(1) << array.Size() << " trace events present";
  *out_num_events = array.Size();
  return true;
}

bool VerifyTestEventsFromFxt(const std::string& test_output_file,
                             trace::TraceReader::RecordConsumer record_consumer) {
  size_t num_errors = 0;
  auto error_handler = [&num_errors](fbl::String error) {
    ++num_errors;
    if (num_errors <= kMaxErrorCount) {
      FX_LOGS(ERROR) << "While reading records got error: " << error.c_str();
    }
    if (num_errors == kMaxErrorCount) {
      FX_LOGS(ERROR) << "Remaining errors will not be printed";
    }
  };

  std::unique_ptr<trace::FileReader> reader;
  if (!trace::FileReader::Create(test_output_file.c_str(), std::move(record_consumer),
                                 std::move(error_handler), &reader)) {
    FX_LOGS(ERROR) << "Error creating trace::FileReader";
    return false;
  }

  reader->ReadFile();
  return num_errors == 0;
}

void FillBuffer(size_t num_times, size_t buffer_size_in_mb) {
  FX_DCHECK(num_times && buffer_size_in_mb);
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
      FX_NOTREACHED();
  }

  return static_cast<size_t>(static_cast<double>(buffer_size / kRecordSize) *
                             percentage_buffer_filled);
}

bool VerifyFullBuffer(const std::string& test_output_file, tracing::BufferingMode buffering_mode,
                      size_t buffer_size_in_mb) {
  size_t num_events;
  if (!VerifyTestEventsFromJson(test_output_file, &num_events)) {
    return false;
  }

  size_t min_num_events = GetMinimumNumberOfEvents(buffering_mode, buffer_size_in_mb);
  if (num_events < min_num_events) {
    FX_LOGS(ERROR) << "Insufficient number of events present, got " << num_events
                   << ", expected at least " << min_num_events;
    return false;
  }

  return true;
}

bool WaitForTracingToStart(async::Loop& loop, zx::duration timeout) {
  trace::TraceObserver trace_observer;

  auto on_trace_state_changed = [&loop]() {
    // Any state change is relevant to us. If we're not started then we must
    // have transitioned from STOPPED to STARTED to at least STOPPING.
    loop.Quit();
  };

  trace_observer.Start(loop.dispatcher(), std::move(on_trace_state_changed));
  if (trace_state() == TRACE_STARTED) {
    return true;
  }

  async::TaskClosure timeout_task([&loop] { loop.Quit(); });
  timeout_task.PostDelayed(loop.dispatcher(), timeout);
  zx_status_t status = loop.Run();
  if (status != ZX_OK && status != ZX_ERR_CANCELED) {
    FX_LOGS(ERROR) << "loop.Run() failed, status=" << status;
    return false;
  }

  status = loop.ResetQuit();
  FX_CHECK(status == ZX_OK);

  return trace_state() == TRACE_STARTED;
}

}  // namespace test
}  // namespace tracing

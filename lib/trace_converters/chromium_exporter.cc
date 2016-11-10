// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <utility>

#include "apps/tracing/lib/trace_converters/chromium_exporter.h"
#include "lib/ftl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace tracing {
namespace {

bool IsEventTypeSupported(reader::TraceEventType type) {
  switch (type) {
    case internal::TraceEventType::kDurationBegin:
    case internal::TraceEventType::kDurationEnd:
    case internal::TraceEventType::kAsyncStart:
    case internal::TraceEventType::kAsyncInstant:
    case internal::TraceEventType::kAsyncEnd:
      return true;
    default:
      break;
  }

  return false;
}

}  // namespace

ChromiumExporter::ChromiumExporter(std::ostream& out)
    : wrapper_(out), writer_(wrapper_) {
  writer_.StartArray();
}

ChromiumExporter::~ChromiumExporter() {
  writer_.EndArray();
}

void ChromiumExporter::ExportRecord(const reader::Record& record) {
  switch (record.type()) {
    case internal::RecordType::kEvent:
      ExportEvent(record.GetEventRecord());
      break;
    default:
      break;
  }
}

void ChromiumExporter::ExportEvent(const reader::EventRecord& event) {
  if (!IsEventTypeSupported(event.event_type))
    return;

  writer_.StartObject();

  writer_.Key("name");
  writer_.String(event.name.data(), event.name.size());
  writer_.Key("cat");
  writer_.String(event.cat.data(), event.cat.size());
  writer_.Key("ts");
  writer_.Uint64(event.timestamp / 1000);
  writer_.Key("pid");
  writer_.Uint64(event.thread.process_koid);
  writer_.Key("tid");
  writer_.Uint64(event.thread.thread_koid);

  switch (event.event_type) {
    case internal::TraceEventType::kDurationBegin:
      writer_.Key("ph");
      writer_.String("B");
      break;
    case internal::TraceEventType::kDurationEnd:
      writer_.Key("ph");
      writer_.String("E");
      break;
    case internal::TraceEventType::kAsyncStart:
      writer_.Key("ph");
      writer_.String("b");
      writer_.Key("id");
      writer_.Uint64(event.event_data.GetAsyncBegin().id);
      break;
    case internal::TraceEventType::kAsyncInstant:
      writer_.Key("ph");
      writer_.String("n");
      writer_.Key("id");
      writer_.Uint64(event.event_data.GetAsyncInstant().id);
      break;
    case internal::TraceEventType::kAsyncEnd:
      writer_.Key("ph");
      writer_.String("e");
      writer_.Key("id");
      writer_.Uint64(event.event_data.GetAsyncEnd().id);
      break;
    default:
      break;
  }

  if (event.arguments.size() > 0) {
    writer_.Key("args");
    writer_.StartObject();
    for (const auto& arg : event.arguments) {
      switch (arg.value.type()) {
        case internal::ArgumentType::kInt32:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.Int(arg.value.GetInt32());
          break;
        case internal::ArgumentType::kInt64:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.Int64(arg.value.GetInt64());
          break;
        case internal::ArgumentType::kDouble:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.Double(arg.value.GetDouble());
          break;
        case internal::ArgumentType::kString:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.String(arg.value.GetString().data(),
                         arg.value.GetString().size());
          break;
        case internal::ArgumentType::kPointer:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.String(
              ftl::StringPrintf("0x%" PRIx64, arg.value.GetPointer()).c_str());
          break;
        case internal::ArgumentType::kKernelObjectId:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.String(
              ftl::StringPrintf("#%" PRIu64, arg.value.GetKernelObjectId())
                  .c_str());
          break;
        default:
          break;
      }
    }
    writer_.EndObject();
  }

  writer_.EndObject();
}

}  // namespace tracing

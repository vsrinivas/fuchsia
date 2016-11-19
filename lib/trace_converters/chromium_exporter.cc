// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace_converters/chromium_exporter.h"

#include <inttypes.h>

#include <utility>

#include "lib/ftl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace tracing {
namespace {

bool IsEventTypeSupported(EventType type) {
  switch (type) {
    case EventType::kDurationBegin:
    case EventType::kDurationEnd:
    case EventType::kAsyncStart:
    case EventType::kAsyncInstant:
    case EventType::kAsyncEnd:
      return true;
    default:
      break;
  }

  return false;
}

}  // namespace

ChromiumExporter::ChromiumExporter(std::ofstream file_out)
    : file_out_(std::move(file_out)), wrapper_(file_out_), writer_(wrapper_) {
  Start();
}

ChromiumExporter::ChromiumExporter(std::ostream& out)
    : wrapper_(out), writer_(wrapper_) {
  Start();
}

ChromiumExporter::~ChromiumExporter() {
  writer_.EndArray();
}

void ChromiumExporter::Start() {
  writer_.StartArray();
}

void ChromiumExporter::ExportRecord(const reader::Record& record) {
  switch (record.type()) {
    case RecordType::kEvent:
      ExportEvent(record.GetEventRecord());
      break;
    default:
      break;
  }
}

void ChromiumExporter::ExportEvent(const reader::EventRecord& event) {
  if (!IsEventTypeSupported(event.type()))
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

  switch (event.type()) {
    case EventType::kDurationBegin:
      writer_.Key("ph");
      writer_.String("B");
      break;
    case EventType::kDurationEnd:
      writer_.Key("ph");
      writer_.String("E");
      break;
    case EventType::kAsyncStart:
      writer_.Key("ph");
      writer_.String("b");
      writer_.Key("id");
      writer_.Uint64(event.event_data.GetAsyncBegin().id);
      break;
    case EventType::kAsyncInstant:
      writer_.Key("ph");
      writer_.String("n");
      writer_.Key("id");
      writer_.Uint64(event.event_data.GetAsyncInstant().id);
      break;
    case EventType::kAsyncEnd:
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
        case ArgumentType::kInt32:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.Int(arg.value.GetInt32());
          break;
        case ArgumentType::kInt64:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.Int64(arg.value.GetInt64());
          break;
        case ArgumentType::kDouble:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.Double(arg.value.GetDouble());
          break;
        case ArgumentType::kString:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.String(arg.value.GetString().data(),
                         arg.value.GetString().size());
          break;
        case ArgumentType::kPointer:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.String(
              ftl::StringPrintf("0x%" PRIx64, arg.value.GetPointer()).c_str());
          break;
        case ArgumentType::kKernelObjectId:
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

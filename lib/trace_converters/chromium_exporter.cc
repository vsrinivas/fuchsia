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

constexpr char kProcessArgKey[] = "process";

constexpr double NanosecondsToMicroseconds(uint64_t micros) {
  return micros * 0.001;
}

bool IsEventTypeSupported(EventType type) {
  switch (type) {
    case EventType::kInstant:
    case EventType::kCounter:
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

const reader::ArgumentValue* GetArgumentValue(
    const std::vector<reader::Argument>& arguments,
    const char* name) {
  for (const auto& arg : arguments) {
    if (arg.name == name)
      return &arg.value;
  }
  return nullptr;
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
  writer_.EndObject();
}

void ChromiumExporter::Start() {
  writer_.StartObject();
  writer_.Key("displayTimeUnit");
  writer_.String("ms");
  writer_.Key("traceEvents");
  writer_.StartArray();
}

void ChromiumExporter::ExportRecord(const reader::Record& record) {
  switch (record.type()) {
    case RecordType::kEvent:
      ExportEvent(record.GetEvent());
      break;
    case RecordType::kKernelObject:
      ExportKernelObject(record.GetKernelObject());
      break;
    default:
      break;
  }
}

void ChromiumExporter::ExportEvent(const reader::Record::Event& event) {
  if (!IsEventTypeSupported(event.type()))
    return;

  writer_.StartObject();

  writer_.Key("cat");
  writer_.String(event.category.data(), event.category.size());
  writer_.Key("name");
  writer_.String(event.name.data(), event.name.size());
  writer_.Key("ts");
  writer_.Double(NanosecondsToMicroseconds(event.timestamp));
  writer_.Key("pid");
  writer_.Uint64(event.process_thread.process_koid);
  writer_.Key("tid");
  writer_.Uint64(event.process_thread.thread_koid);

  switch (event.type()) {
    case EventType::kInstant:
      writer_.Key("ph");
      writer_.String("i");
      writer_.Key("s");
      switch (event.data.GetInstant().scope) {
        case EventScope::kGlobal:
          writer_.String("g");
          break;
        case EventScope::kProcess:
          writer_.String("p");
          break;
        case EventScope::kThread:
        default:
          writer_.String("t");
          break;
      }
      break;
    case EventType::kCounter:
      writer_.Key("ph");
      writer_.String("C");
      break;
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
      writer_.Uint64(event.data.GetAsyncBegin().id);
      break;
    case EventType::kAsyncInstant:
      writer_.Key("ph");
      writer_.String("n");
      writer_.Key("id");
      writer_.Uint64(event.data.GetAsyncInstant().id);
      break;
    case EventType::kAsyncEnd:
      writer_.Key("ph");
      writer_.String("e");
      writer_.Key("id");
      writer_.Uint64(event.data.GetAsyncEnd().id);
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
        case ArgumentType::kKoid:
          writer_.Key(arg.name.data(), arg.name.size());
          writer_.String(
              ftl::StringPrintf("#%" PRIu64, arg.value.GetKoid()).c_str());
          break;
        default:
          break;
      }
    }
    writer_.EndObject();
  }

  writer_.EndObject();
}

void ChromiumExporter::ExportKernelObject(
    const reader::Record::KernelObject& kernel_object) {
  switch (kernel_object.object_type) {
    case MX_OBJ_TYPE_PROCESS: {
      writer_.StartObject();
      writer_.Key("ph");
      writer_.String("M");
      writer_.Key("name");
      writer_.String("process_name");
      writer_.Key("pid");
      writer_.Uint64(kernel_object.koid);
      writer_.Key("args");
      writer_.StartObject();
      writer_.Key("name");
      writer_.String(kernel_object.name.data(), kernel_object.name.size());
      writer_.EndObject();
      writer_.EndObject();
      break;
    }
    case MX_OBJ_TYPE_THREAD: {
      const reader::ArgumentValue* process_arg =
          GetArgumentValue(kernel_object.arguments, kProcessArgKey);
      if (!process_arg || process_arg->type() != ArgumentType::kKoid)
        break;
      writer_.StartObject();
      writer_.Key("ph");
      writer_.String("M");
      writer_.Key("name");
      writer_.String("thread_name");
      writer_.Key("pid");
      writer_.Uint64(process_arg->GetKoid());
      writer_.Key("tid");
      writer_.Uint64(kernel_object.koid);
      writer_.Key("args");
      writer_.StartObject();
      writer_.Key("name");
      writer_.String(kernel_object.name.data(), kernel_object.name.size());
      writer_.EndObject();
      writer_.EndObject();
      break;
    }
    default:
      break;
  }
}

}  // namespace tracing

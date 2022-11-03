// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/ktrace_provider/importer.h"

#include <lib/fxt/fields.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/clock.h>
#include <zircon/syscalls.h>

#include <iomanip>
#include <iterator>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>

#include "src/performance/ktrace_provider/reader.h"

namespace ktrace_provider {

#define MAKE_STRING(literal) trace_context_make_registered_string_literal(context_, literal)

Importer::Importer(trace_context_t* context)
    : context_(context),
      tags_(GetTags()),
      kernel_string_ref_(MAKE_STRING("kernel")),
      unknown_category_ref_(MAKE_STRING("kernel:unknown")),
      arch_category_ref_(MAKE_STRING("kernel:arch")),
      meta_category_ref_(MAKE_STRING("kernel:meta")),
      lifecycle_category_ref_(MAKE_STRING("kernel:lifecycle")),
      tasks_category_ref_(MAKE_STRING("kernel:tasks")),
      ipc_category_ref_(MAKE_STRING("kernel:ipc")),
      irq_category_ref_(MAKE_STRING("kernel:irq")),
      probe_category_ref_(MAKE_STRING("kernel:probe")),
      sched_category_ref_(MAKE_STRING("kernel:sched")),
      syscall_category_ref_(MAKE_STRING("kernel:syscall")),
      channel_category_ref_(MAKE_STRING("kernel:channel")),
      vcpu_category_ref_(MAKE_STRING("kernel:vcpu")),
      vm_category_ref_(MAKE_STRING("kernel:vm")),
      arg0_name_ref_(MAKE_STRING("arg0")),
      arg1_name_ref_(MAKE_STRING("arg1")),
      kUnknownThreadRef(trace_make_unknown_thread_ref()) {}

#undef MAKE_STRING

Importer::~Importer() = default;

bool Importer::Import(Reader& reader) {
  trace_context_write_process_info_record(context_, kNoProcess, &kernel_string_ref_);

  auto start = zx::clock::get_monotonic();

  while (true) {
    if (auto record = reader.ReadNextRecord()) {
      // A record with a group bitfield of 0 is a padding record.  It contains
      // no info, and is just used to pad the kernel's ring buffer to maintain
      // continuity when need.  Skip it.
      if (KTRACE_GROUP(record->tag) == 0) {
        FX_VLOGS(5) << "Skipped ktrace padding record, tag=0x" << std::hex << record->tag;
        continue;
      }

      if (KTRACE_GROUP(record->tag) & KTRACE_GRP_FXT) {
        const size_t fxt_record_size = KTRACE_LEN(record->tag) - sizeof(uint64_t);
        const uint64_t* fxt_record = reinterpret_cast<const uint64_t*>(record) + 1;

        // Verify that the FXT record header specifies the correct size.
        const size_t fxt_size_from_header =
            fxt::RecordFields::RecordSize::Get<size_t>(fxt_record[0]) * sizeof(uint64_t);
        if (fxt_size_from_header != fxt_record_size) {
          FX_LOGS(ERROR) << "Found fxt record of size " << fxt_record_size
                         << " bytes whose header indicates a record of size "
                         << fxt_size_from_header << " bytes. Skipping.";
          continue;
        }

        void* dst = trace_context_alloc_record(context_, fxt_record_size);
        if (dst != nullptr) {
          memcpy(dst, reinterpret_cast<const char*>(fxt_record), fxt_record_size);
        }

        if (fxt::RecordFields::Type::Get<fxt::RecordType>(fxt_record[0]) ==
            fxt::RecordType::kString) {
          HandleFxtStringRecord(fxt_record);
        }

        switch (KTRACE_EVENT(record->tag)) {
          case KTRACE_EVENT(TAG_THREAD_NAME):
            HandleFxtThreadName(fxt_record);
            break;
          default:
            break;
        }

        continue;
      }

      if (!ImportRecord(record, KTRACE_LEN(record->tag))) {
        FX_VLOGS(5) << "Skipped ktrace record, tag=0x" << std::hex << record->tag;
      }
    } else {
      break;
    }
  }

  size_t nr_bytes_read = reader.number_bytes_read();
  size_t nr_records_read = reader.number_records_read();

  // This is an INFO and not VLOG() as we currently always want to see this.
  FX_LOGS(INFO) << "Import of " << nr_records_read << " ktrace records"
                << "(" << nr_bytes_read
                << " bytes) took: " << (zx::clock::get_monotonic() - start).to_usecs() << "us";

  return true;
}

bool Importer::ImportRecord(const ktrace_header_t* record, size_t record_size) {
  auto it = tags_.find(KTRACE_EVENT(record->tag));
  if (it != tags_.end()) {
    const TagInfo& tag_info = it->second;
    switch (tag_info.type) {
      case TagType::kBasic:
        FX_LOGS(WARNING) << "Found basic record that is expected to be migrated to FXT.";
        return false;
      case TagType::kQuad:
        FX_LOGS(WARNING) << "Found quad record that is expected to be migrated to FXT.";
        return false;
      case TagType::kName:
        if (sizeof(ktrace_rec_name_t) > record_size)
          return false;
        return ImportNameRecord(reinterpret_cast<const ktrace_rec_name_t*>(record), tag_info);
    }
  }

  // TODO(eieio): Using this combination of bits and groups to select the record
  // type is a bit hacky due to how the kernel trace record is defined. Fixing
  // this requires a re-design or replacement with the same strategy used in the
  // rest of the system.
  const bool is_probe_group = KTRACE_GROUP(record->tag) & KTRACE_GRP_PROBE;
  const bool is_flow = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_FLOW;
  const bool is_begin = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
  const bool is_end = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;
  const bool is_duration = !is_flow && (is_begin ^ is_end);
  const bool is_counter = !is_flow && is_begin && is_end;

  if (is_probe_group)
    return ImportProbeRecord(record, record_size);
  else if (is_duration)
    return ImportDurationRecord(record, record_size);
  else if (is_flow)
    return ImportFlowRecord(record, record_size);
  else if (is_counter)
    return ImportCounterRecord(record, record_size);

  return ImportUnknownRecord(record, record_size);
}

bool Importer::ImportNameRecord(const ktrace_rec_name_t* record, const TagInfo& tag_info) {
  std::string_view name(record->name, strnlen(record->name, ZX_MAX_NAME_LEN - 1));
  FX_VLOGS(5) << "NAME: tag=0x" << std::hex << record->tag << " (" << tag_info.name << "), id=0x"
              << record->id << ", arg=0x" << record->arg << ", name='" << fbl::String(name).c_str()
              << "'";

  switch (KTRACE_EVENT(record->tag)) {
    case KTRACE_EVENT(TAG_THREAD_NAME):
      return HandleThreadName(record->id, record->arg, name);
    case KTRACE_EVENT(TAG_PROC_NAME):
      return HandleProcessName(record->id, name);
    case KTRACE_EVENT(TAG_IRQ_NAME):
      return HandleIRQName(record->id, name);
    case KTRACE_EVENT(TAG_PROBE_NAME):
      return HandleProbeName(record->id, name);
    default:
      return false;
  }
}

bool Importer::ImportProbeRecord(const ktrace_header_t* record, size_t record_size) {
  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;

  if (record_size == 24) {
    const auto arg0 = reinterpret_cast<const uint32_t*>(record + 1)[0];
    const auto arg1 = reinterpret_cast<const uint32_t*>(record + 1)[1];
    FX_VLOGS(5) << "PROBE: tag=0x" << std::hex << record->tag << ", event_name_id=0x"
                << event_name_id << ", tid=" << std::dec << record->tid << ", ts=" << record->ts
                << ", arg0=0x" << std::hex << arg0 << ", arg1=0x" << arg1;
    return HandleProbe(record->ts, record->tid, event_name_id, cpu_trace, arg0, arg1);
  } else if (record_size == 32) {
    const auto arg0 = reinterpret_cast<const uint64_t*>(record + 1)[0];
    const auto arg1 = reinterpret_cast<const uint64_t*>(record + 1)[1];
    FX_VLOGS(5) << "PROBE: tag=0x" << std::hex << record->tag << ", event_name_id=0x"
                << event_name_id << ", tid=" << std::dec << record->tid << ", ts=" << record->ts
                << ", arg0=0x" << std::hex << arg0 << ", arg1=0x" << arg1;
    return HandleProbe(record->ts, record->tid, event_name_id, cpu_trace, arg0, arg1);
  }

  FX_VLOGS(5) << "PROBE: tag=0x" << std::hex << record->tag << ", event_name_id=0x" << event_name_id
              << ", tid=" << std::dec << record->tid << ", ts=" << record->ts;
  return HandleProbe(record->ts, record->tid, event_name_id, cpu_trace);
}

bool Importer::ImportDurationRecord(const ktrace_header_t* record, size_t record_size) {
  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const uint32_t group = KTRACE_GROUP(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;
  const bool is_begin = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
  const bool is_end = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;

  if (record_size == 32) {
    const auto arg0 = reinterpret_cast<const uint64_t*>(record + 1)[0];
    const auto arg1 = reinterpret_cast<const uint64_t*>(record + 1)[1];
    if (is_begin) {
      return HandleDurationBegin(record->ts, record->tid, event_name_id, group, cpu_trace, arg0,
                                 arg1);
    } else if (is_end) {
      return HandleDurationEnd(record->ts, record->tid, event_name_id, group, cpu_trace, arg0,
                               arg1);
    }
  } else {
    if (is_begin) {
      return HandleDurationBegin(record->ts, record->tid, event_name_id, group, cpu_trace);
    } else if (is_end) {
      return HandleDurationEnd(record->ts, record->tid, event_name_id, group, cpu_trace);
    }
  }

  return false;
}

bool Importer::ImportFlowRecord(const ktrace_header_t* record, size_t record_size) {
  FX_DCHECK(KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_FLOW);

  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const uint32_t group = KTRACE_GROUP(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;
  const bool is_begin = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
  const bool is_end = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;

  if (record_size == 32) {
    const auto flow_id = reinterpret_cast<const uint64_t*>(record + 1)[0];
    if (is_begin && !is_end) {
      return HandleFlowBegin(record->ts, record->tid, event_name_id, group, cpu_trace, flow_id);
    }
    if (is_end && !is_begin) {
      return HandleFlowEnd(record->ts, record->tid, event_name_id, group, cpu_trace, flow_id);
    }
    if (is_begin && is_end) {
      return HandleFlowStep(record->ts, record->tid, event_name_id, group, cpu_trace, flow_id);
    }
    return ImportUnknownRecord(record, record_size);
  }

  return false;
}

bool Importer::ImportCounterRecord(const ktrace_header_t* record, size_t record_size) {
  FX_DCHECK((KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_COUNTER) == KTRACE_FLAGS_COUNTER);

  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const uint32_t group = KTRACE_GROUP(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;

  if (record_size == 32) {
    const auto counter_id = reinterpret_cast<const uint64_t*>(record + 1)[0];
    const auto value = reinterpret_cast<const int64_t*>(record + 1)[1];
    return HandleCounter(record->ts, record->tid, event_name_id, group, cpu_trace, counter_id,
                         value);
  }

  return false;
}

bool Importer::ImportUnknownRecord(const ktrace_header_t* record, size_t record_size) {
  FX_VLOGS(5) << "UNKNOWN: tag=0x" << std::hex << record->tag << ", size=" << std::dec
              << record_size;
  return false;
}

bool Importer::HandleFxtThreadName(const uint64_t* record) {
  const uint64_t header = record[0];
  if (fxt::RecordFields::Type::Get<fxt::RecordType>(header) != fxt::RecordType::kKernelObject) {
    return false;
  }
  const size_t num_args = fxt::KernelObjectRecordFields::ArgumentCount::Get<size_t>(header);
  const zx_koid_t thread = record[1];
  zx_koid_t process = ZX_KOID_INVALID;
  // Scan argument list to find the process koid, if specified. First, read the
  // name ref to skip over any inline name.
  const uint64_t* next_arg = record + 2;
  const uint32_t name_ref = fxt::KernelObjectRecordFields::NameStringRef::Get<uint32_t>(header);
  if (name_ref & 0x8000) {
    next_arg += fxt::WordSize::FromBytes(name_ref & 0x7FFF).SizeInWords();
  }
  for (size_t i = 0; i < num_args; i++) {
    const uint64_t arg_header = next_arg[0];
    const size_t arg_size = fxt::ArgumentFields::ArgumentSize::Get<size_t>(arg_header);
    const fxt::ArgumentType arg_type =
        fxt::ArgumentFields::Type::Get<fxt::ArgumentType>(arg_header);
    if (arg_type == fxt::ArgumentType::kKoid) {
      const uint32_t name_ref = fxt::ArgumentFields::NameRef::Get<uint32_t>(arg_header);
      std::string arg_name;
      zx_koid_t koid;
      if (name_ref & 0x8000) {
        const size_t name_length = name_ref & 0x7FFF;
        arg_name = std::string(reinterpret_cast<const char*>(next_arg + 1), name_length);
        koid = *(next_arg + 1 + fxt::WordSize::FromBytes(name_length).SizeInWords());
      } else {
        arg_name = fxt_string_table_[name_ref];
        koid = next_arg[1];
      }
      if (arg_name == "process") {
        process = koid;
        break;
      }
    }

    next_arg += arg_size;
  }
  thread_refs_.emplace(thread, trace_context_make_registered_thread(context_, process, thread));
  return true;
}

bool Importer::HandleFxtStringRecord(const uint64_t* record) {
  const uint32_t index = fxt::StringRecordFields::StringIndex::Get<uint32_t>(record[0]);
  const size_t length = fxt::StringRecordFields::StringLength::Get<size_t>(record[0]);
  fxt_string_table_.emplace(index, std::string(reinterpret_cast<const char*>(record + 1), length));
  return true;
}

bool Importer::HandleThreadName(zx_koid_t thread, zx_koid_t process, std::string_view name) {
  FX_LOGS(ERROR) << "Found KTrace thread name record, which is expected to be "
                 << "migrated to FXT.";
  return false;
}

bool Importer::HandleProcessName(zx_koid_t process, std::string_view name) {
  FX_LOGS(ERROR) << "Found KTrace process name record, which is expected to be "
                 << "migrated to FXT.";
  return false;
}

bool Importer::HandleIRQName(uint32_t irq, std::string_view name) {
  irq_names_.emplace(
      irq, trace_context_make_registered_string_copy(context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleProbeName(uint32_t event_name_id, std::string_view name) {
  probe_names_.emplace(event_name_id, trace_context_make_registered_string_copy(
                                          context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleProbe(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                           bool cpu_trace) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_context_write_instant_event_record(context_, event_time, &thread_ref, &probe_category_ref_,
                                           &name_ref, TRACE_SCOPE_THREAD, nullptr, 0u);
  return true;
}

bool Importer::HandleProbe(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                           bool cpu_trace, uint32_t arg0, uint32_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_arg_t args[] = {trace_make_arg(arg0_name_ref_, trace_make_uint32_arg_value(arg0)),
                        trace_make_arg(arg1_name_ref_, trace_make_uint32_arg_value(arg1))};
  trace_context_write_instant_event_record(context_, event_time, &thread_ref, &probe_category_ref_,
                                           &name_ref, TRACE_SCOPE_THREAD, args, std::size(args));
  return true;
}

bool Importer::HandleProbe(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                           bool cpu_trace, uint64_t arg0, uint64_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_arg_t args[] = {trace_make_arg(arg0_name_ref_, trace_make_uint64_arg_value(arg0)),
                        trace_make_arg(arg1_name_ref_, trace_make_uint64_arg_value(arg1))};
  trace_context_write_instant_event_record(context_, event_time, &thread_ref, &probe_category_ref_,
                                           &name_ref, TRACE_SCOPE_THREAD, args, std::size(args));
  return true;
}

bool Importer::HandleDurationBegin(trace_ticks_t event_time, zx_koid_t thread,
                                   uint32_t event_name_id, uint32_t group, bool cpu_trace) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_begin_event_record(context_, event_time, &thread_ref, &category_ref,
                                                  &name_ref, nullptr, 0u);

  return true;
}

bool Importer::HandleDurationBegin(trace_ticks_t event_time, zx_koid_t thread,
                                   uint32_t event_name_id, uint32_t group, bool cpu_trace,
                                   uint64_t arg0, uint64_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_arg_t args[] = {trace_make_arg(arg0_name_ref_, trace_make_uint64_arg_value(arg0)),
                        trace_make_arg(arg1_name_ref_, trace_make_uint64_arg_value(arg1))};
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_begin_event_record(context_, event_time, &thread_ref, &category_ref,
                                                  &name_ref, args, std::size(args));

  return true;
}

bool Importer::HandleDurationEnd(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                                 uint32_t group, bool cpu_trace) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_end_event_record(context_, event_time, &thread_ref, &category_ref,
                                                &name_ref, nullptr, 0u);

  return true;
}

bool Importer::HandleDurationEnd(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                                 uint32_t group, bool cpu_trace, uint64_t arg0, uint64_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_arg_t args[] = {trace_make_arg(arg0_name_ref_, trace_make_uint64_arg_value(arg0)),
                        trace_make_arg(arg1_name_ref_, trace_make_uint64_arg_value(arg1))};
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_end_event_record(context_, event_time, &thread_ref, &category_ref,
                                                &name_ref, args, std::size(args));

  return true;
}

bool Importer::HandleFlowBegin(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                               uint32_t group, bool cpu_trace, trace_flow_id_t flow_id) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_flow_begin_event_record(context_, event_time, &thread_ref, &category_ref,
                                              &name_ref, flow_id, nullptr, 0u);

  return true;
}

bool Importer::HandleFlowEnd(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                             uint32_t group, bool cpu_trace, trace_flow_id_t flow_id) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_flow_end_event_record(context_, event_time, &thread_ref, &category_ref,
                                            &name_ref, flow_id, nullptr, 0u);

  return true;
}

bool Importer::HandleFlowStep(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                              uint32_t group, bool cpu_trace, trace_flow_id_t flow_id) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_flow_step_event_record(context_, event_time, &thread_ref, &category_ref,
                                             &name_ref, flow_id, nullptr, 0u);

  return true;
}

bool Importer::HandleCounter(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                             uint32_t group, bool cpu_trace, trace_counter_id_t counter_id,
                             int64_t value) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(static_cast<trace_cpu_number_t>(thread))
                : GetThreadRef(thread);
  trace_arg_t args[] = {trace_make_arg(arg0_name_ref_, trace_make_int64_arg_value(value))};
  trace_string_ref_t name_ref = GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_counter_event_record(context_, event_time, &thread_ref, &category_ref,
                                           &name_ref, counter_id, args, std::size(args));

  return true;
}

trace_thread_ref_t Importer::GetCpuCurrentThreadRef(trace_cpu_number_t cpu_number) {
  if (cpu_number >= cpu_infos_.size())
    return kUnknownThreadRef;
  return cpu_infos_[cpu_number].current_thread_ref;
}

zx_koid_t Importer::GetCpuCurrentThread(trace_cpu_number_t cpu_number) {
  if (cpu_number >= cpu_infos_.size())
    return ZX_KOID_INVALID;
  return cpu_infos_[cpu_number].current_thread;
}

trace_thread_ref_t Importer::SwitchCpuToThread(trace_cpu_number_t cpu_number, zx_koid_t thread) {
  if (cpu_number >= cpu_infos_.size())
    cpu_infos_.resize(cpu_number + 1u);
  cpu_infos_[cpu_number].current_thread = thread;
  return cpu_infos_[cpu_number].current_thread_ref = GetThreadRef(thread);
}

const trace_string_ref_t& Importer::GetNameRef(
    std::unordered_map<uint32_t, trace_string_ref_t>& table, const char* kind, uint32_t id) {
  auto it = table.find(id);
  if (it == table.end()) {
    fbl::String name = fbl::StringPrintf("%s %#x", kind, id);
    std::tie(it, std::ignore) = table.emplace(
        id, trace_context_make_registered_string_copy(context_, name.data(), name.length()));
  }
  return it->second;
}

const trace_thread_ref_t& Importer::GetThreadRef(zx_koid_t thread) {
  // |trace_make_inline_thread_ref()| requires a valid thread id (given that
  // we're using ZX_KOID_INVALID for the process for unknown threads).
  if (thread == ZX_KOID_INVALID) {
    return kUnknownThreadRef;
  }
  auto it = thread_refs_.find(thread);
  if (it == thread_refs_.end()) {
    std::tie(it, std::ignore) =
        thread_refs_.emplace(thread, trace_make_inline_thread_ref(kNoProcess, thread));
  }
  return it->second;
}

// TODO(fxbug.dev/27430): Revisit using pseudo thread references to support per-CPU
// events.
const trace_thread_ref_t& Importer::GetCpuPseudoThreadRef(trace_cpu_number_t cpu) {
  const zx_koid_t thread = kKernelPseudoCpuBase + cpu;
  auto it = thread_refs_.find(thread);
  if (it == thread_refs_.end()) {
    fbl::String label = fbl::StringPrintf("cpu-%d", cpu);

    trace_string_ref name_ref = trace_make_inline_string_ref(label.data(), label.length());
    trace_context_write_thread_info_record(context_, kNoProcess, thread, &name_ref);
    std::tie(it, std::ignore) = thread_refs_.emplace(
        thread, trace_context_make_registered_thread(context_, kNoProcess, thread));
  }
  return it->second;
}

const trace_string_ref_t& Importer::GetCategoryForGroup(uint32_t group) {
  switch (group) {
    case KTRACE_GRP_META:
      return meta_category_ref_;
    case KTRACE_GRP_LIFECYCLE:
      return lifecycle_category_ref_;
    case KTRACE_GRP_SCHEDULER:
      return sched_category_ref_;
    case KTRACE_GRP_TASKS:
      return tasks_category_ref_;
    case KTRACE_GRP_IPC:
      return ipc_category_ref_;
    case KTRACE_GRP_IRQ:
      return irq_category_ref_;
    case KTRACE_GRP_SYSCALL:
      return syscall_category_ref_;
    case KTRACE_GRP_PROBE:
      return probe_category_ref_;
    case KTRACE_GRP_ARCH:
      return arch_category_ref_;
    case KTRACE_GRP_VM:
      return vm_category_ref_;
    default:
      return unknown_category_ref_;
  }
}

}  // namespace ktrace_provider

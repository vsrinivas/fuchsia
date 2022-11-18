// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/memory/profile/fxt_to_pprof.h"

#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <optional>
#include <set>
#include <vector>

#include "src/performance/memory/profile/memory_layout.h"
#include "src/performance/memory/profile/profile.pb.h"
#include "src/performance/memory/profile/stack_compression.h"
#include "src/performance/memory/profile/trace_constants.h"

namespace {

class StringTable {
 public:
  StringTable(google::protobuf::RepeatedPtrField<std::string>* table) : table_(table) {}
  uint64_t intern(std::string value) {
    if (table_->empty()) {
      // Add an element to index 0, because indexes keys are strictly positive in pprof format.
      table_->Add("");
    }

    auto itr = value_to_index_.find(value);
    if (itr != value_to_index_.end()) {
      return itr->second;
    }
    uint64_t index = value_to_index_[value] = table_->size();
    table_->Add(std::move(value));
    return index;
  }

 private:
  std::unordered_map<std::string, uint64_t> value_to_index_;
  google::protobuf::RepeatedPtrField<std::string>* table_;
};

std::optional<std::reference_wrapper<const trace::Record::Event>> get_instant_event(
    const trace::Record& record, const char* category) {
  if (record.type() != trace::RecordType::kEvent) {
    return std::nullopt;
  }
  auto& event = record.GetEvent();
  if (event.category != category) {
    return std::nullopt;
  }
  return event;
}

std::optional<std::reference_wrapper<const trace::LargeRecordData::BlobEvent>> get_blob_event(
    const trace::Record& record, const char* category) {
  if (record.type() != trace::RecordType::kLargeRecord) {
    return std::nullopt;
  }
  auto& blob = record.GetLargeRecord().GetBlob();
  if (!std::holds_alternative<trace::LargeRecordData::BlobEvent>(blob)) {
    return std::nullopt;
  }
  auto& blob_event = std::get<trace::LargeRecordData::BlobEvent>(blob);
  if (blob_event.category != category) {
    return std::nullopt;
  }
  return {blob_event};
}

std::optional<uint64_t> get_int64_value(const fbl::Vector<trace::Argument>& arguments,
                                        const char* name) {
  for (auto& argument : arguments) {
    if (argument.name() == name) {
      return {argument.value().GetUint64()};
    }
  }
  return std::nullopt;
}

// Builds a set of deallocation events.
fit::result<std::string, std::set<std::pair<uint64_t, trace_ticks_t>>> get_all_deallocations(
    const RecordContainer& records, const char* category) {
  std::set<std::pair<uint64_t, trace_ticks_t>> deallocations;
  if (!records.ForEach([&](const trace::Record& record) {
        auto event = get_instant_event(record, category);
        if (!event) {
          return;
        }
        if (event->get().name == DEALLOC) {
          std::optional<uint64_t> address = get_int64_value(event->get().arguments, ADDR);
          if (address) {
            deallocations.insert({*address, event->get().timestamp});
            // Intentionnaly ignore duplicated events if any.
          }
        }
      })) {
    return fit::error("Could not read trace records. Is the file accessible?");
  }

  return fit::ok(deallocations);
}

}  // namespace

fit::result<std::string, perfetto::third_party::perftools::profiles::Profile> fxt_to_profile(
    const RecordContainer& records, const char* category) {
  // Pprof profile to be returned.
  perfetto::third_party::perftools::profiles::Profile pprof;
  // Hold deallocated address and timestamp pair, to be matched with allocations.
  auto deallocations_result = get_all_deallocations(records, category);
  if (deallocations_result.is_error()) {
    return fit::error(deallocations_result.error_value());
  }
  std::set<std::pair<uint64_t, trace_ticks_t>> deallocations =
      std::move(deallocations_result.value());
  // Set of record unique identifiers used to discard duplicated records.
  std::set<uint64_t> trace_ids;
  // Map of memory regions used to verify backtraces addresses.
  std::map<uint64_t, perfetto::third_party::perftools::profiles::Mapping> end_address_to_mapping;
  // Set of all code pointer addresses.
  std::unordered_set<uint64_t> location_addresses;
  StringTable string_table(pprof.mutable_string_table());

  // Add constants to the string tables.
  {
    auto* sample_type = pprof.add_sample_type();
    sample_type->set_type(string_table.intern("new object"));
    sample_type->set_unit(string_table.intern("count"));
  }
  {
    auto* sample_type = pprof.add_sample_type();
    sample_type->set_type(string_table.intern("new allocated"));
    sample_type->set_unit(string_table.intern("bytes"));
  }
  {
    auto* sample_type = pprof.add_sample_type();
    sample_type->set_type(string_table.intern("residual object"));
    sample_type->set_unit(string_table.intern("count"));
  }
  {
    auto* sample_type = pprof.add_sample_type();
    sample_type->set_type(string_table.intern("residual allocated"));
    sample_type->set_unit(string_table.intern("bytes"));
  }
  pprof.set_default_sample_type(1);

  uint64_t allocation_count = 0;
  uint64_t deallocation_count = 0;
  uint64_t duplicate_count = 0;
  uint64_t layout_count = 0;

  if (!records.ForEach([&](const trace::Record& record) {
        auto blob_event = get_blob_event(record, category);
        if (!blob_event) {
          return;
        }
        std::optional<uint64_t> trace_id = get_int64_value(blob_event->get().arguments, TRACE_ID);
        if (!trace_id) {
          std::cerr << "Warning: Skip malformed record. Missing '" << TRACE_ID
                    << "' argument: " << record.ToString() << std::endl;
          return;
        }
        if (!trace_ids.insert(*trace_id).second) {
          // This is a duplicated message.
          // TODO(https://fxbug.dev/111062): Remove this workaround.
          duplicate_count++;
          return;
        }

        if (blob_event->get().name == ALLOC) {
          allocation_count++;
          std::optional<uint64_t> size = get_int64_value(blob_event->get().arguments, SIZE);
          if (!size) {
            std::cerr << "Warning: Malformed allocation record: `" << SIZE << "` is missing."
                      << std::endl;
            return;
          }
          auto sample = pprof.add_sample();
          sample->add_value(1);
          sample->add_value(size.value_or(0));
          sample->add_value(0);
          sample->add_value(0);

          uint64_t pc_buffer[255];
          for (uint64_t pc : decompress({reinterpret_cast<const uint8_t*>(blob_event->get().blob),
                                         blob_event->get().blob_size},
                                        pc_buffer)) {
            if (pc == 0) {
              std::cerr << "Warning: " << blob_event->get().name << " "
                        << blob_event->get().timestamp << std::endl;
            }
            sample->add_location_id(pc);
            location_addresses.insert(pc);
          }

          std::optional<uint64_t> address = get_int64_value(blob_event->get().arguments, ADDR);
          if (!address) {
            std::cerr << "Warning: Malformed allocation record: `" << ADDR << "` is missing."
                      << std::endl;
            return;
          }

          auto itr = deallocations.lower_bound({*address, blob_event->get().timestamp});
          if (itr == deallocations.end() || itr->first != *address) {
            // This has never been deallocated.
            sample->set_value(2, sample->value(0));
            sample->set_value(3, sample->value(1));
          } else {
            // This element was deallocated. Clear the map.
            deallocations.erase(itr);
          }
        } else if (blob_event->get().name == LAYOUT) {
          layout_count++;
          Layout layout;
          std::istringstream is(std::string(reinterpret_cast<const char*>(blob_event->get().blob),
                                            blob_event->get().blob_size));
          layout.Read(is);

          for (auto mmap : layout.mmaps) {
            auto* mapping = pprof.add_mapping();
            mapping->set_id(pprof.mapping_size());
            mapping->set_memory_start(mmap.starting_address);
            mapping->set_memory_limit(mmap.starting_address + mmap.size);
            mapping->set_file_offset(mmap.relative_addr);
            mapping->set_build_id(string_table.intern(layout.modules[mmap.module_index].ToHex()));
            end_address_to_mapping[mapping->memory_limit()] = *mapping;
          }
        }
      })) {
    return fit::error("Could not read trace records. Is the file accessible?");
  }

  if (trace_ids.size()) {
    auto itr = trace_ids.begin();
    auto previous = *itr++;
    while (itr != trace_ids.end()) {
      auto current = *itr;
      if (current != previous + 1) {
        std::cerr << "Warning: " << current - previous << " traces lost between call " << current
                  << " and " << previous << std::endl;
      }
      previous = current;
      current = *itr++;
    }
  }

  for (uint64_t address : location_addresses) {
    auto* location = pprof.add_location();
    location->set_id(address);
    location->set_address(address);
    auto itr = end_address_to_mapping.upper_bound(address);
    if (itr == end_address_to_mapping.end()) {
      std::cerr << "Warning: No mapping matched addr: " << address << std::endl;
      continue;
    }
    if (itr->second.memory_start() > address) {
      std::cerr << "Warning: Memory mapping out range addr: " << address << std::endl;
      continue;
    }
    location->set_mapping_id(itr->second.id());
  }

  if (duplicate_count) {
    std::cerr << "Warning: removed " << duplicate_count
              << " duplicated records. This is expected until (fxb/111062) is fixed." << std::endl;
  }

  std::cerr << "Processed " << allocation_count << " allocation and " << deallocation_count
            << " deallocation records." << std::endl;

  if (allocation_count == 0 && deallocation_count == 0) {
    return fit::error(
        "The trace is empty. This is either because:\n"
        "1 - the executable does not dynamically load libmemory_trace.so\n"
        "    Please verify that the binary depends dynamically on the library\n"
        "    with `readelf -d <binary>`\n"
        "2 - the component does not have access to the trace FILD service\n"
        "    `fuchsia.tracing.provider.Registry`\n"
        "    Please verify you manifest.\n"
        "3 - the component is not running");
  } else if (layout_count == 0) {
    return fit::error(
        "memory layout not found in the trace.\n "
        "It is not possible to symbolize the profile without the location of binaries in memory.\n"
        "This most likely happened because of the buffering mode.\n"
        "https://fuchsia.dev/fuchsia-src/concepts/kernel/tracing-provider-buffering-modes?hl=en");
  }
  return fit::ok(pprof);
}

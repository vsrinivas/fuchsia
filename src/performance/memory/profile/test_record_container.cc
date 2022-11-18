// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/memory/profile/test_record_container.h"

#include <elf-search.h>
#include <zircon/process.h>

#include <cinttypes>

#include "src/performance/memory/profile/stack_compression.h"
#include "src/performance/memory/profile/trace_constants.h"

namespace {

zx_koid_t get_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

std::optional<std::reference_wrapper<const trace::LargeRecordData::BlobEvent>> get_blob_event(
    const trace::Record& record) {
  if (record.type() != trace::RecordType::kLargeRecord) {
    return std::nullopt;
  }
  auto& blob = record.GetLargeRecord().GetBlob();
  if (!std::holds_alternative<trace::LargeRecordData::BlobEvent>(blob)) {
    return std::nullopt;
  }
  auto& blob_event = std::get<trace::LargeRecordData::BlobEvent>(blob);
  return {blob_event};
}

std::optional<std::reference_wrapper<const trace::Record::Event>> get_instant_event(
    const trace::Record& record) {
  if (record.type() != trace::RecordType::kEvent) {
    return std::nullopt;
  }
  auto& event = record.GetEvent();
  return {event};
}

}  // namespace

TestRecordContainer::TestRecordContainer() {}

bool TestRecordContainer::ForEach(std::function<void(const trace::Record&)> record_consumer) const {
  for (const auto& record : records_) {
    record_consumer(record);
  }
  return true;
}

const fbl::Vector<trace::Record>& TestRecordContainer::records() const { return records_; }

const fbl::Vector<trace::Record>& TestRecordContainer::removed() const { return removed_; }

bool TestRecordContainer::ReadFromFixture() {
  fbl::Vector<trace::Record> records;
  if (!fixture_read_records(&records)) {
    return false;
  }
  // Remove all records from other threads.
  const zx_koid_t tid_self = get_koid(zx_thread_self());
  for (auto& record : records) {
    auto blob = get_blob_event(record);
    if (blob &&
        (blob->get().name == LAYOUT || blob->get().process_thread.thread_koid() == tid_self)) {
      records_.push_back(std::move(record));
      continue;
    }
    auto instant = get_instant_event(record);
    if (instant && instant->get().process_thread.thread_koid() == tid_self) {
      records_.push_back(std::move(record));
      continue;
    }
    removed_.push_back(std::move(record));
  }

  return true;
}

std::ostream& operator<<(std::ostream& os, const TestRecordContainer& container) {
  os << "{{{reset}}}" << std::endl;
  zx_handle_t process = zx_process_self();
  elf_search::ForEachModule(
      *zx::unowned_process{process}, [&os, count = 0u](const elf_search::ModuleInfo& info) mutable {
        const size_t kPageSize = zx_system_get_page_size();
        unsigned int module_id = count++;
        // Print out the module first.
        char buf[255];
        sprintf(buf, "{{{module:%#x:%s:elf:", module_id, info.name.begin());
        os << buf;
        for (uint8_t byte : info.build_id) {
          sprintf(buf, "%02x", byte);
          os << buf;
        }
        os << "}}}\n";
        // Now print out the various segments.
        for (const auto& phdr : info.phdrs) {
          if (phdr.p_type != PT_LOAD) {
            continue;
          }
          uintptr_t start = phdr.p_vaddr & -kPageSize;
          uintptr_t end = (phdr.p_vaddr + phdr.p_memsz + kPageSize - 1) & -kPageSize;
          sprintf(buf, "{{{mmap:%#" PRIxPTR ":%#" PRIxPTR ":load:%#x:", info.vaddr + start,
                  end - start, module_id);
          os << buf;
          if (phdr.p_flags & PF_R) {
            os << "r";
          }
          if (phdr.p_flags & PF_W) {
            os << "w";
          }
          if (phdr.p_flags & PF_X) {
            os << "x";
          }
          sprintf(buf, ":%#" PRIxPTR "}}}\n", start);
          os << buf;
        }
      });

  for (size_t i = 0; i < container.records().size(); i++) {
    os << "[" << i << "] " << container.records()[i].ToString() << std::endl;
    auto blob_event = get_blob_event(container.records()[i]);
    if (blob_event) {
      if (blob_event->get().name == ALLOC || blob_event->get().name == DEALLOC) {
        uint64_t pc_buffer[255];
        int frame_index = 0;
        for (uint64_t pc : decompress({reinterpret_cast<const uint8_t*>(blob_event->get().blob),
                                       blob_event->get().blob_size},
                                      pc_buffer)) {
          char buf[255];
          sprintf(buf, "{{{bt:%d:%#" PRIxPTR ":ra}}}", frame_index++, pc);
          os << buf << std::endl;
        }
      }
    }
  }
  os << "==== Removed===" << std::endl;
  for (size_t i = 0; i < container.removed().size(); i++) {
    os << "[" << i << "] " << container.removed()[i].ToString() << std::endl;
  }
  return os;
}

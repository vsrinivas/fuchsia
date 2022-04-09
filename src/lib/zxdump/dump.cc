// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zxdump/dump.h"

#include <zircon/assert.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core.h"

namespace zxdump {

// The public class is just a container for a std::unique_ptr to this private
// class, so no the implementation details of the object need to be visible in
// the public header.
class ProcessDumpBase::Collector {
 public:
  // Only constructed by Emplace.
  Collector() = delete;

  // Only Emplace and clear call this.  The process is mandatory and all other
  // members are safely default-initialized.
  explicit Collector(zx::unowned_process process) : process_(std::move(process)) {
    ZX_ASSERT(process_->is_valid());
  }

  void clear() { *this = Collector{std::move(process_)}; }

  // This collects information about memory and other process-wide state.  The
  // return value gives the total size of the ET_CORE file to be written.
  // Collection is cut short without error if the ET_CORE file would already
  // exceed the size limit without even including the memory.
  fitx::result<Error, size_t> CollectProcess(size_t limit) {
    // Clear out from any previous use.
    phdrs_.clear();

    // Now figure everything else out to write out a full ET_CORE file.
    return fitx::ok(Layout());
  }

  // Accumulate header and note data to be written out, by calling
  // `dump(offset, ByreView{...})` repeatedly.
  size_t DumpHeaders(DumpCallback dump, size_t limit) {
    // Layout has already been done.
    ZX_ASSERT(ehdr_.type == elfldltl::ElfType::kCore);

    size_t offset = 0;
    auto append = [&](ByteView data) -> bool {
      if (offset >= limit || limit - offset < data.size()) {
        return false;
      }
      bool bail = dump(offset, data);
      offset += data.size();
      return bail;
    };

    // Generate the ELF headers.
    if (append({reinterpret_cast<std::byte*>(&ehdr_), sizeof(ehdr_)})) {
      return offset;
    }
    if (ehdr_.shnum > 0) {
      ZX_DEBUG_ASSERT(ehdr_.shnum() == 1);
      ZX_DEBUG_ASSERT(ehdr_.shoff == offset);
      if (append({reinterpret_cast<std::byte*>(&shdr_), sizeof(shdr_)})) {
        return offset;
      }
    }
    if (append({reinterpret_cast<std::byte*>(phdrs_.data()), phdrs_.size() * sizeof(phdrs_[0])})) {
      return offset;
    }

    return offset;
  }

 private:
  // Populate the header fields and reify phdrs_ with p_offset values.
  // This chooses where everything will go in the ET_CORE file.
  size_t Layout() {
    // Fill in the file header boilerplate.
    ehdr_.magic = Elf::Ehdr::kMagic;
    ehdr_.elfclass = elfldltl::ElfClass::k64;
    ehdr_.elfdata = elfldltl::ElfData::k2Lsb;
    ehdr_.ident_version = elfldltl::ElfVersion::kCurrent;
    ehdr_.type = elfldltl::ElfType::kCore;
    ehdr_.machine = elfldltl::ElfMachine::kNative;
    ehdr_.version = elfldltl::ElfVersion::kCurrent;
    size_t offset = ehdr_.phoff = ehdr_.ehsize = sizeof(ehdr_);
    ehdr_.phentsize = sizeof(phdrs_[0]);
    offset += phdrs_.size() * sizeof(phdrs_[0]);
    if (phdrs_.size() < Elf::Ehdr::kPnXnum) {
      ehdr_.phnum = static_cast<uint16_t>(phdrs_.size());
    } else {
      shdr_.info = static_cast<uint32_t>(phdrs_.size());
      ehdr_.phnum = Elf::Ehdr::kPnXnum;
      ehdr_.shnum = 1;
      ehdr_.shentsize = sizeof(shdr_);
      ehdr_.shoff = offset;
      offset += sizeof(shdr_);
    }

    // Now assign offsets to all the segments.
    for (auto& phdr : phdrs_) {
      switch (phdr.type) {
          // TODO(mcgrathr): nothing generates any segments yet

        default:
          ZX_ASSERT_MSG(false, "generated p_type %#x ???", phdr.type());
      }
    }

    return offset;
  }

  zx::unowned_process process_;

  std::vector<Elf::Phdr> phdrs_;
  Elf::Ehdr ehdr_ = {};
  Elf::Shdr shdr_ = {};  // Only used for the PN_XNUM case.
};

ProcessDumpBase::~ProcessDumpBase() = default;

void ProcessDumpBase::clear() { collector_->clear(); }

size_t ProcessDumpBase::DumpHeadersImpl(DumpCallback dump, size_t limit) {
  return collector_->DumpHeaders(std::move(dump), limit);
}

fitx::result<Error, size_t> ProcessDumpBase::Collect(size_t limit) {
  return collector_->CollectProcess(limit);
}

// The Collector borrows the process handle.  A single Collector cannot be
// used for a different process later.  It can be clear()'d to reset all
// state other than the process handle and the process being suspended.
void ProcessDumpBase::Emplace(zx::unowned_process process) {
  collector_ = std::make_unique<Collector>(std::move(process));
}

template <>
ProcessDump<zx::process>::ProcessDump(zx::process process) : process_{std::move(process)} {
  Emplace(zx::unowned_process{process_});
}

template class ProcessDump<zx::unowned_process>;

template <>
ProcessDump<zx::unowned_process>::ProcessDump(zx::unowned_process process)
    : process_{std::move(process)} {
  Emplace(zx::unowned_process{process_});
}

}  // namespace zxdump

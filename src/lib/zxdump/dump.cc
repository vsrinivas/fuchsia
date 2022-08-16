// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zxdump/dump.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

#include "core.h"

namespace zxdump {

namespace {

// This collects a bunch of note data, header and payload ByteView items.  The
// actual data the items point to is stored in Collector::notes_.
class NoteData {
 public:
  using Vector = std::vector<ByteView>;

  NoteData() = default;
  NoteData(NoteData&&) = default;
  NoteData& operator=(NoteData&&) = default;

  size_t size() const { return data_.size(); }

  size_t size_bytes() const { return size_bytes_; }

  void push_back(ByteView data) {
    ZX_DEBUG_ASSERT(data.size() % NoteAlign() == 0);
    if (!data.empty()) {
      data_.push_back(data);
      size_bytes_ += data.size();
    }
  }

  Vector take() && { return std::move(data_); }

 private:
  Vector data_;
  size_t size_bytes_ = 0;
};

// This represents one note header, with name and padding but no desc.
template <size_t NameSize>
class NoteHeader {
 public:
  NoteHeader() = delete;
  constexpr NoteHeader(const NoteHeader&) = default;
  constexpr NoteHeader& operator=(const NoteHeader&) = default;

  constexpr NoteHeader(std::string_view name, uint32_t descsz, uint32_t type)
      : nhdr_{.namesz = NameSize + 1, .descsz = descsz, .type = type} {
    assert(name.size() == NameSize);
    name.copy(name_, NameSize);
  }

  ByteView bytes() const {
    return {
        reinterpret_cast<const std::byte*>(this),
        sizeof(*this),
    };
  }

  constexpr void set_size(uint32_t descsz) { nhdr_.descsz = descsz; }

 private:
  static constexpr size_t kAlignedSize_ = NoteAlign(NameSize + 1);
  static_assert(kAlignedSize_ < std::numeric_limits<uint32_t>::max());

  Elf::Nhdr nhdr_;
  char name_[kAlignedSize_] = {};
};

// Each note format has an object in notes_ of a NoteBase type.
// The Type is the n_type field for the ELF note header.
// The Class represents a set of notes handled the same way.
// It provides the ELF note name, as well as other details used below.
template <typename Class, uint32_t Type>
class NoteBase {
 public:
  void AddToNoteData(NoteData& notes) const {
    if (!data_.empty()) {
      notes.push_back(header_.bytes());
      notes.push_back(data_);
    }
  }

  bool empty() const { return data_.empty(); }

  const auto& header() const { return header_; }
  auto& header() { return header_; }

  size_t size_bytes() const { return empty() ? 0 : header_.bytes().size() + data_.size(); }

  void clear() { data_ = {}; }

 protected:
  // The subclass Collect method calls this when it might have data.
  void Emplace(ByteView data) {
    if (!data.empty()) {
      ZX_ASSERT(data.size() <= std::numeric_limits<uint32_t>::max());
      header_.set_size(static_cast<uint32_t>(data.size()));
      data_ = data;
    }
  }

 private:
  // This holds the storage for the note header that NoteData points into.
  decltype(Class::MakeHeader(Type)) header_ = Class::MakeHeader(Type);

  // This points into the subclass storage for the note contents.
  // It's empty until the subclass calls Emplace.
  ByteView data_;
};

// Each class derived from NoteBase uses a core.h kFooNoteName constant in:
// ```
//   static constexpr auto MakeHeader = kMakeNote<kFooNoteName>;
// ```
template <const std::string_view& Name>
constexpr auto kMakeNote = [](uint32_t type) { return NoteHeader<Name.size()>(Name, 0, type); };

// This is called with each note (classes derived from NoteBase) when its
// information is required.  It can be called more than once, so it does
// nothing if it's already collected the data.  Each NoteBase subclass below
// has a Collect(const Handle&)->fitx::result<Error> method that should call
// NoteBase::Emplace when it has acquired data, and then won't be called again.
constexpr auto CollectNote = [](const auto& handle, auto& note) -> fitx::result<Error> {
  if (note.empty()) {
    return note.Collect(handle);
  }
  return fitx::ok();
};

// This doesn't need to be templated as specifically as InfoNote itself.  The
// GetInfo work is the same for all get_info topics whose data have the same
// size and alignment, so it's factored out that way here.

template <size_t Size, size_t Align>
using AlignedStorageVector = std::vector<std::aligned_storage_t<Size, Align>>;

template <typename T>
using InfoVector = AlignedStorageVector<sizeof(T), alignof(T)>;

template <size_t Align, size_t Size>
fitx::result<Error, AlignedStorageVector<Size, Align>> GetInfo(
    const zx::handle& task, zx_object_info_topic_t topic, AlignedStorageVector<Size, Align> data) {
  // Start with a buffer of at least one but reuse any larger old buffer.
  if (data.empty()) {
    data.resize(1);
  }
  while (true) {
    // Use as much space as is handy.
    data.resize(data.capacity());

    // See how much there is available and how much fits in the buffer.
    size_t actual, avail;
    zx_status_t status = task.get_info(topic, data.data(), data.size() * Size, &actual, &avail);
    if (status != ZX_OK) {
      return fitx::error(Error{"zx_object_get_info", status});
    }

    if (actual == avail) {
      // This is all the data.
      data.resize(avail);
      data.shrink_to_fit();
      return fitx::ok(std::move(data));
    }

    // There is more data.  Make the buffer at least as big as is needed.
    if (data.size() < avail) {
      data.resize(avail);
    }
  }
}

// Notes based on zx_object_get_info calls use this.
// For some types, the size is variable.
// We treat them all as variable.
template <typename Class, zx_object_info_topic_t Topic, typename T>
class InfoNote : public NoteBase<Class, Topic> {
 public:
  fitx::result<Error> Collect(const typename Class::Handle& task) {
    auto result =
        GetInfo<alignof(T), sizeof(T)>(*zx::unowned_handle{task.get()}, Topic, std::move(data_));
    if (result.is_error()) {
      return result.take_error();
    }
    data_ = std::move(result).value();
    this->Emplace({
        reinterpret_cast<const std::byte*>(data_.data()),
        data_.size() * sizeof(data_[0]),
    });
    return fitx::ok();
  }

  cpp20::span<const T> info() const {
    static_assert(sizeof(T) == sizeof(data_.data()[0]));
    return {reinterpret_cast<const T*>(data_.data()), data_.size()};
  }

 private:
  InfoVector<T> data_;
};

// Notes based on the fixed-sized property/state calls use this.
template <typename Class, uint32_t Prop, typename T>
class PropertyNote : public NoteBase<Class, Prop> {
 public:
  fitx::result<Error> Collect(const typename Class::Handle& handle) {
    zx_status_t status = (handle.*Class::kSyscall)(Prop, &data_, sizeof(T));
    if (status != ZX_OK) {
      return fitx::error(Error{Class::kCall_, status});
    }
    this->Emplace({reinterpret_cast<std::byte*>(&data_), sizeof(data_)});
    return fitx::ok();
  }

 private:
  T data_;
};

// Classes using zx_object_get_property use this.
struct PropertyBaseClass {
  static constexpr std::string_view kCall_{"zx_object_get_property"};
  static constexpr auto kSyscall = &zx::object_base::get_property;
};

// These are called via std::apply on ProcessNotes tuples.

constexpr auto CollectNoteData =
    // For each note that hasn't already been fetched, try to fetch it now.
    [](const auto& handle, auto&... note) -> fitx::result<Error, size_t> {
  // This value is always replaced (or ignored), but the type is not
  // default-constructible.
  fitx::result<Error> result = fitx::ok();

  size_t total = 0;
  auto collect = [&handle, &result, &total](auto& note) -> bool {
    result = CollectNote(handle, note);
    if (result.is_ok()) {
      total += note.size_bytes();
      return true;
    }
    switch (result.error_value().status_) {
      case ZX_ERR_NOT_SUPPORTED:
      case ZX_ERR_BAD_STATE:
        // These just mean the data is not available because it never
        // existed or the thread is dead.
        return true;

      default:
        return false;
    }
  };

  if (!(collect(note) && ...)) {
    // The fold expression bailed out early after setting result.
    return result.take_error();
  }

  return fitx::ok(total);
};

constexpr auto DumpNoteData =
    // Return a vector pointing at all the nonempty note data.
    [](const auto&... note) -> NoteData::Vector {
  NoteData data;
  (note.AddToNoteData(data), ...);
  return std::move(data).take();
};

}  // namespace

// The public class is just a container for a std::unique_ptr to this private
// class, so no implementation details of the object need to be visible in the
// public header.
class ProcessDumpBase::Collector {
 public:
  // Only constructed by Emplace.
  Collector() = delete;

  // Only Emplace and clear call this.  The process is mandatory and all other
  // members are safely default-initialized.  Note memory_ can't be initialized
  // in its declaration since that uses process_ before it's been set here.
  explicit Collector(zx::unowned_process process)
      : process_(std::move(process)), memory_(*process_) {
    ZX_ASSERT(process_->is_valid());
  }

  // Reset to initial state.
  void clear() { *this = Collector{std::move(process_)}; }

  // This collects information about memory and other process-wide state.  The
  // return value gives the total size of the ET_CORE file to be written.
  // Collection is cut short without error if the ET_CORE file would already
  // exceed the size limit without even including the memory.
  fitx::result<Error, size_t> CollectProcess(SegmentCallback prune, size_t limit) {
    // Collect the process-wide note data.
    auto collect = [this](auto&... note) -> fitx::result<Error, size_t> {
      return CollectNoteData(*process_, note...);
    };
    if (auto result = std::apply(collect, notes_); result.is_error()) {
      return result.take_error();
    } else {
      notes_size_bytes_ += result.value();
    }

    // Clear out from any previous use.
    phdrs_.clear();

    // The first phdr is the main note segment.
    const Elf::Phdr note_phdr = {
        .type = elfldltl::ElfPhdrType::kNote,
        .flags = Elf::Phdr::kRead,
        .filesz = notes_size_bytes_,
        .align = NoteAlign(),
    };
    phdrs_.push_back(note_phdr);

    // Find the memory segments and build IDs.  This fills the phdrs_ table.
    if (auto result = FindMemory(std::move(prune)); result.is_error()) {
      return result.take_error();
    }

    // Now figure everything else out to write out a full ET_CORE file.
    return fitx::ok(Layout());
  }

  // Accumulate header and note data to be written out, by calling
  // `dump(offset, ByreView{...})` repeatedly.
  // The views point to storage in this->notes.
  fitx::result<Error, size_t> DumpHeaders(DumpCallback dump, size_t limit) {
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
      return fitx::ok(offset);
    }
    if (ehdr_.shnum > 0) {
      ZX_DEBUG_ASSERT(ehdr_.shnum() == 1);
      ZX_DEBUG_ASSERT(ehdr_.shoff == offset);
      if (append({reinterpret_cast<std::byte*>(&shdr_), sizeof(shdr_)})) {
        return fitx::ok(offset);
      }
    }
    if (append({reinterpret_cast<std::byte*>(phdrs_.data()), phdrs_.size() * sizeof(phdrs_[0])})) {
      return fitx::ok(offset);
    }

    // Generate the process-wide note data.
    for (ByteView data : notes()) {
      if (append(data)) {
        return fitx::ok(offset);
      }
    }
    ZX_DEBUG_ASSERT(offset % NoteAlign() == 0);
    return fitx::ok(offset);
  }

  // Dump the memory data by calling `dump(size_t offset, ByteView data)` with
  // the data meant for the given offset into the ET_CORE file.  The data is in
  // storage only available during the callback.  The `dump` function returns
  // some fitx::result<error_type> type.  DumpMemory returns an "error" result
  // for errors reading the memory in.  The "success" result holds the results
  // from the callback.  If `dump` returns an error result, that is returned
  // immediately.  If it returns success, additional callbacks will be made
  // until all the data has been dumped, and the final `dump` callback's return
  // value will be the "success" return value.
  fitx::result<Error, size_t> DumpMemory(DumpCallback dump, size_t limit) {
    size_t offset = notes_size_bytes_;
    for (const auto& segment : phdrs_) {
      if (segment.type == elfldltl::ElfPhdrType::kLoad) {
        uintptr_t vaddr = segment.vaddr;
        offset = segment.offset;
        if (offset >= limit) {
          break;
        }
        size_t size = std::min(segment.filesz(), limit - offset);
        while (size > 0) {
          ByteView chunk;

          // This yields some nonempty subset of the requested range.
          if (auto read = memory_.ReadBytes(vaddr, 1, size); read.is_error()) {
            return read.take_error();
          } else {
            chunk = read.value();
            ZX_DEBUG_ASSERT(chunk.size() <= size);
            ZX_DEBUG_ASSERT(!chunk.empty());
            ZX_DEBUG_ASSERT(chunk.data());
          }

          // Send it to the callback to write it out.
          if (dump(offset, chunk)) {
            break;
          }

          vaddr += chunk.size();
          offset += chunk.size();
          size -= chunk.size();
        }
      }
    }
    return fitx::ok(offset);
  }

 private:
  struct ProcessInfoClass {
    using Handle = zx::process;
    static constexpr auto MakeHeader = kMakeNote<kProcessInfoNoteName>;
  };

  template <zx_object_info_topic_t Topic, typename T>
  using ProcessInfo = InfoNote<ProcessInfoClass, Topic, T>;

  struct ProcessPropertyClass : public PropertyBaseClass {
    using Handle = zx::process;
    static constexpr auto MakeHeader = kMakeNote<kProcessPropertyNoteName>;
  };

  template <uint32_t Prop, typename T>
  using ProcessProperty = PropertyNote<ProcessPropertyClass, Prop, T>;

  using ProcessNotes = std::tuple<
      // This lists all the notes for process-wide state.  Ordering of the
      // notes after the first two is not specified and can change.
      ProcessInfo<ZX_INFO_HANDLE_BASIC, zx_info_handle_basic_t>,
      ProcessProperty<ZX_PROP_NAME, char[ZX_MAX_NAME_LEN]>,
      ProcessInfo<ZX_INFO_PROCESS, zx_info_process_t>,
      ProcessInfo<ZX_INFO_PROCESS_THREADS, zx_koid_t>,
      ProcessInfo<ZX_INFO_TASK_STATS, zx_info_task_stats_t>,
      ProcessInfo<ZX_INFO_TASK_RUNTIME, zx_info_task_runtime_t>,
      ProcessInfo<ZX_INFO_PROCESS_MAPS, zx_info_maps_t>,
      ProcessInfo<ZX_INFO_PROCESS_VMOS, zx_info_vmo_t>,
      ProcessInfo<ZX_INFO_PROCESS_HANDLE_STATS, zx_info_process_handle_stats_t>,
      ProcessInfo<ZX_INFO_HANDLE_TABLE, zx_info_handle_extended_t>,
      ProcessProperty<ZX_PROP_PROCESS_DEBUG_ADDR, uintptr_t>,
      ProcessProperty<ZX_PROP_PROCESS_BREAK_ON_LOAD, uintptr_t>,
      ProcessProperty<ZX_PROP_PROCESS_VDSO_BASE_ADDRESS, uintptr_t>,
      ProcessProperty<ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID, uintptr_t>>;

  // Returns a vector of views into the storage held in this->notes_.
  NoteData::Vector notes() const { return std::apply(DumpNoteData, notes_); }

  // Some of the process-wide state is needed in Suspend anyway, so pre-collect
  // it directly in the notes.

  auto& process_threads() {
    return std::get<ProcessInfo<ZX_INFO_PROCESS_THREADS, zx_koid_t>>(notes_);
  }

  auto& process_maps() {
    return std::get<ProcessInfo<ZX_INFO_PROCESS_MAPS, zx_info_maps_t>>(notes_);
  }

  auto& process_vmos() {
    return std::get<ProcessInfo<ZX_INFO_PROCESS_VMOS, zx_info_vmo_t>>(notes_);
  }

  // Populate phdrs_.  The p_offset fields are filled in later by Layout.
  fitx::result<Error> FindMemory(SegmentCallback prune_segment) {
    // Make sure we have the relevant information to scan.
    if (auto result = CollectNote(*process_, process_maps()); result.is_error()) {
      if (result.error_value().status_ == ZX_ERR_NOT_SUPPORTED) {
        // This just means there is no information in the dump.
        return fitx::ok();
      }
      return result;
    }
    if (auto result = CollectNote(*process_, process_vmos()); result.is_error()) {
      if (result.error_value().status_ == ZX_ERR_NOT_SUPPORTED) {
        // This just means there is no information in the dump.
        return fitx::ok();
      }
      return result;
    }

    // The mappings give KOID and some info but the VMO info is also needed.
    // So make a quick cross-reference table to find one from the other.
    std::map<zx_koid_t, const zx_info_vmo_t&> vmos;
    for (const zx_info_vmo_t& info : process_vmos().info()) {
      vmos.emplace(info.koid, info);
    }

    constexpr auto elf_flags = [](zx_vm_option_t mmu_flags) -> Elf::Word {
      return (((mmu_flags & ZX_VM_PERM_READ) ? Elf::Phdr::kRead : 0) |
              ((mmu_flags & ZX_VM_PERM_WRITE) ? Elf::Phdr::kWrite : 0) |
              ((mmu_flags & ZX_VM_PERM_EXECUTE) ? Elf::Phdr::kExecute : 0));
    };

    // Go through each mapping.  They are in ascending address order.
    uintptr_t address_limit = 0;
    for (const zx_info_maps_t& info : process_maps().info()) {
      if (info.type == ZX_INFO_MAPS_TYPE_MAPPING) {
        ZX_ASSERT(info.base % ZX_PAGE_SIZE == 0);
        ZX_ASSERT(info.size % ZX_PAGE_SIZE == 0);
        ZX_ASSERT(info.base >= address_limit);
        address_limit = info.base + info.size;
        ZX_ASSERT(info.base < address_limit);

        // Add a PT_LOAD segment for the mapping no matter what.
        // It will be present with p_filesz==0 if the memory is elided.
        {
          const Elf::Phdr new_phdr = {
              .type = elfldltl::ElfPhdrType::kLoad,
              .flags = elf_flags(info.u.mapping.mmu_flags),
              .vaddr = info.base,
              .filesz = info.size,
              .memsz = info.size,
              .align = zx_system_get_page_size(),
          };
          phdrs_.push_back(new_phdr);
        }
        Elf::Phdr& segment = phdrs_.back();

        const zx_info_vmo_t& vmo = vmos.at(info.u.mapping.vmo_koid);
        ZX_DEBUG_ASSERT(vmo.koid == info.u.mapping.vmo_koid);

        // The default-constructed state elides the whole segment.
        SegmentDisposition dump;

        // Default choice: dump the whole thing.  But never dump device memory,
        // which could cause side effects on memory-mapped devices just from
        // reading the physical address.
        if (ZX_INFO_VMO_TYPE(vmo.flags) != ZX_INFO_VMO_TYPE_PHYSICAL) {
          dump.filesz = segment.filesz;
        }

        // Let the callback decide about this segment.
        if (auto result = prune_segment(dump, info, vmo); result.is_error()) {
          return result.take_error();
        } else {
          dump = result.value();
        }

        ZX_ASSERT(dump.filesz <= info.size);
        segment.filesz = dump.filesz;
      }
    }

    return fitx::ok();
  }

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
    auto place = [&offset](Elf::Phdr& phdr) {
      if (phdr.filesz == 0) {
        phdr.offset = 0;
      } else {
        offset = (offset + phdr.align - 1) & -size_t{phdr.align};
        phdr.offset = offset;
        offset += phdr.filesz;
      }
    };

    // First is the initial note segment.
    ZX_DEBUG_ASSERT(!phdrs_.empty());
    ZX_DEBUG_ASSERT(phdrs_[0].type == elfldltl::ElfPhdrType::kNote);
    place(phdrs_[0]);

    // Now place the remaining segments, if any.
    for (auto& phdr : cpp20::span(phdrs_).subspan(1)) {
      switch (phdr.type) {
        case elfldltl::ElfPhdrType::kLoad:
          place(phdr);
          break;

        default:
          ZX_ASSERT_MSG(false, "generated p_type %#x ???", phdr.type());
      }
    }

    ZX_DEBUG_ASSERT(offset % NoteAlign() == 0);
    return offset;
  }

  class ProcessMemoryReader {
   public:
    // Move-only, not default constructible.
    ProcessMemoryReader() = delete;
    ProcessMemoryReader(const ProcessMemoryReader&) = delete;
    ProcessMemoryReader(ProcessMemoryReader&&) = default;
    ProcessMemoryReader& operator=(const ProcessMemoryReader&) = delete;
    ProcessMemoryReader& operator=(ProcessMemoryReader&&) = default;

    explicit ProcessMemoryReader(const zx::process& proc) : process_(proc) {
      ZX_ASSERT(process_->is_valid());
    }

    // Reset cached state so no old cached data is reused.
    void clear() { *this = ProcessMemoryReader{*process_}; }

    // Read some data from the process's memory at the given address.  The
    // returned view starts at that address and has at least min_bytes data
    // available.  If more data than that is readily available, it will be
    // returned, but no more than max_bytes.  The returned view is valid only
    // until the next use of this ProcessMemoryReader object.
    auto ReadBytes(uintptr_t vaddr, size_t min_bytes, size_t max_bytes = kWindowSize_)
        -> fitx::result<Error, ByteView> {
      ZX_ASSERT(min_bytes > 0);
      ZX_ASSERT(max_bytes >= min_bytes);
      ZX_ASSERT(min_bytes <= kWindowSize_);
      if (vaddr >= buffer_vaddr_ && vaddr - buffer_vaddr_ < valid_size_) {
        ZX_DEBUG_ASSERT(buffer_data().data());
        // There is some cached data already covering the address.
        ByteView data = buffer_data().substr(vaddr - buffer_vaddr_, max_bytes);
        if (data.size() >= min_bytes) {
          ZX_DEBUG_ASSERT(data.data());
          return fitx::ok(data);
        }
      }

      // Read some new data into the buffer.
      if (!buffer_) {
        buffer_ = std::make_unique<Buffer>();
      }
      ZX_DEBUG_ASSERT(buffer_->data());
      valid_size_ = 0;
      buffer_vaddr_ = vaddr;
      max_bytes = std::min(max_bytes, kWindowSize_);

      auto try_read = [&]() {
        ZX_DEBUG_ASSERT(buffer_);
        ZX_DEBUG_ASSERT(buffer_->data());
        return process_->read_memory(buffer_vaddr_, buffer_->data(), max_bytes, &valid_size_);
      };

      // Try to read the chosen maximum.  The call can fail with
      // ZX_ERR_NOT_FOUND in some cases where not all pages are readable
      // addresses, so retry with one page fewer until reading succeeds.
      zx_status_t status = try_read();
      while (status == ZX_ERR_NOT_FOUND && max_bytes >= min_bytes) {
        uintptr_t end_vaddr = buffer_vaddr_ + max_bytes;
        if (end_vaddr % ZX_PAGE_SIZE != 0) {
          // Try again without the partial page.
          end_vaddr &= -uintptr_t{ZX_PAGE_SIZE};
          max_bytes = end_vaddr - buffer_vaddr_;
          status = try_read();
        } else {
          // Try one page fewer.
          end_vaddr -= ZX_PAGE_SIZE;
          max_bytes = end_vaddr - buffer_vaddr_;
          if (end_vaddr > buffer_vaddr_) {
            status = try_read();
          } else {
            break;
          }
        }
      }

      if (status != ZX_OK) {
        return fitx::error(Error{"zx_process_read_memory", status});
      }

      if (valid_size_ < min_bytes) {
        return fitx::error(Error{"short memory read", ZX_ERR_NO_MEMORY});
      }

      ZX_DEBUG_ASSERT(valid_size_ > 0);
      ZX_DEBUG_ASSERT(buffer_);
      ZX_DEBUG_ASSERT(buffer_->data());
      ZX_DEBUG_ASSERT(buffer_data().data());
      ByteView data = buffer_data().substr(0, max_bytes);
      ZX_DEBUG_ASSERT(data.data());
      return fitx::ok(data);
    }

    // Read an array from the given address.
    template <typename T>
    fitx::result<Error, const T*> ReadArray(uintptr_t vaddr, size_t nelem) {
      size_t byte_size = sizeof(T) * nelem;
      if (byte_size > kWindowSize_) {
        return fitx::error(Error{"array too large", ZX_ERR_NO_MEMORY});
      }

      auto result = ReadBytes(vaddr, byte_size);
      if (result.is_error()) {
        return result.take_error();
      }

      if ((vaddr - buffer_vaddr_) % alignof(T) != 0) {
        return fitx::error(Error{"misaligned data", ZX_ERR_NO_MEMORY});
      }

      return fitx::ok(reinterpret_cast<const T*>(result.value().data()));
    }

    // Read a datum from the given address.
    template <typename T>
    fitx::result<Error, std::reference_wrapper<const T>> Read(uintptr_t vaddr) {
      auto result = ReadArray<T>(vaddr, 1);
      if (result.is_error()) {
        return result.take_error();
      }
      const T* datum_ptr = result.value();
      return fitx::ok(std::cref(*datum_ptr));
    }

   private:
    static constexpr size_t kWindowSize_ = 1024;
    using Buffer = std::array<std::byte, kWindowSize_>;

    ByteView buffer_data() const { return {buffer_->data(), valid_size_}; }

    std::unique_ptr<Buffer> buffer_;
    uintptr_t buffer_vaddr_ = 0;
    size_t valid_size_ = 0;
    zx::unowned_process process_;
  };

  zx::unowned_process process_;
  ProcessMemoryReader memory_;
  ProcessNotes notes_;

  std::vector<Elf::Phdr> phdrs_;
  Elf::Ehdr ehdr_ = {};
  Elf::Shdr shdr_ = {};  // Only used for the PN_XNUM case.

  // This collects the totals for process-wide and thread notes.
  size_t notes_size_bytes_ = 0;
};

ProcessDumpBase::~ProcessDumpBase() = default;

void ProcessDumpBase::clear() { collector_->clear(); }

fitx::result<Error, size_t> ProcessDumpBase::CollectProcess(SegmentCallback prune, size_t limit) {
  return collector_->CollectProcess(std::move(prune), limit);
}

fitx::result<Error, size_t> ProcessDumpBase::DumpHeadersImpl(DumpCallback dump, size_t limit) {
  return collector_->DumpHeaders(std::move(dump), limit);
}

fitx::result<Error, size_t> ProcessDumpBase::DumpMemoryImpl(DumpCallback callback, size_t limit) {
  return collector_->DumpMemory(std::move(callback), limit);
}

// The Collector borrows the process handle.  A single Collector cannot be
// used for a different process later.  It can be clear()'d to reset all
// state other than the process handle.
void ProcessDumpBase::Emplace(zx::unowned_process process) {
  collector_ = std::make_unique<Collector>(std::move(process));
}

template <>
ProcessDump<zx::process>::ProcessDump(zx::process process) noexcept : process_{std::move(process)} {
  Emplace(zx::unowned_process{process_});
}

template class ProcessDump<zx::unowned_process>;

template <>
ProcessDump<zx::unowned_process>::ProcessDump(zx::unowned_process process) noexcept
    : process_{std::move(process)} {
  Emplace(zx::unowned_process{process_});
}

}  // namespace zxdump

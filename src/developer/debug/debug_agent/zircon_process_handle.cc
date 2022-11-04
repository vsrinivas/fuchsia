// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_process_handle.h"

#include <algorithm>
#include <iterator>

#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/elf_utils.h"
#include "src/developer/debug/debug_agent/process_handle_observer.h"
#include "src/developer/debug/debug_agent/zircon_exception_handle.h"
#include "src/developer/debug/debug_agent/zircon_thread_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/message_loop_fuchsia.h"
#include "third_party/crashpad/minidump/minidump_file_writer.h"
#include "third_party/crashpad/snapshot/fuchsia/process_snapshot_fuchsia.h"
#include "third_party/crashpad/util/file/string_file.h"
#include "third_party/crashpad/util/fuchsia/scoped_task_suspend.h"

namespace debug_agent {

namespace {

void FillVmoInfo(const zx_info_vmo_t& source, debug_ipc::InfoHandleVmo& dest) {
  static_assert(sizeof(dest.name) == sizeof(source.name));
  memcpy(dest.name, source.name, sizeof(source.name));
  dest.size_bytes = source.size_bytes;
  dest.parent_koid = source.parent_koid;
  dest.num_children = source.num_children;
  dest.num_mappings = source.num_mappings;
  dest.share_count = source.share_count;
  dest.flags = source.flags;
  dest.committed_bytes = source.committed_bytes;
  dest.cache_policy = source.cache_policy;
  dest.metadata_bytes = source.metadata_bytes;
  dest.committed_change_events = source.committed_change_events;
}

debug_ipc::AddressRegion MapToAddressRegion(const zx_info_maps& map) {
  debug_ipc::AddressRegion region;
  region.name = map.name;
  region.base = map.base;
  region.size = map.size;
  region.depth = map.depth;
  region.mmu_flags = map.u.mapping.mmu_flags;
  region.vmo_koid = map.u.mapping.vmo_koid;
  region.vmo_offset = map.u.mapping.vmo_offset;
  region.committed_pages = map.u.mapping.committed_pages;
  return region;
}

}  // namespace

ZirconProcessHandle::ZirconProcessHandle(zx::process p)
    : process_koid_(zircon::KoidForObject(p)), process_(std::move(p)) {}

std::string ZirconProcessHandle::GetName() const { return zircon::NameForObject(process_); }

std::vector<std::unique_ptr<ThreadHandle>> ZirconProcessHandle::GetChildThreads() const {
  std::vector<std::unique_ptr<ThreadHandle>> result;
  for (auto& child : zircon::GetChildThreads(process_))
    result.push_back(std::make_unique<ZirconThreadHandle>(std::move(child)));
  return result;
}

zx_koid_t ZirconProcessHandle::GetJobKoid() const {
  if (job_koid_ == ZX_KOID_INVALID) {
    zx_info_handle_basic_t info;
    // The related_koid of a process is the koid of its parent job and is immutable.
    if (process_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) == ZX_OK)
      job_koid_ = info.related_koid;
  }
  return job_koid_;
}

debug::Status ZirconProcessHandle::Kill() { return debug::ZxStatus(process_.kill()); }

int64_t ZirconProcessHandle::GetReturnCode() const {
  zx_info_process_t info = {};
  if (process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr) == ZX_OK)
    return info.return_code;
  return 0;
}

debug::Status ZirconProcessHandle::Attach(ProcessHandleObserver* observer) {
  FX_DCHECK(observer);
  observer_ = observer;

  if (!process_watch_handle_.watching()) {
    // Start watching.
    debug::MessageLoopFuchsia* loop = debug::MessageLoopFuchsia::Current();
    FX_DCHECK(loop);  // Loop must be created on this thread first.

    // Register for debug exceptions.
    debug::MessageLoopFuchsia::WatchProcessConfig config;
    config.process_name = GetName();
    config.process_handle = process_.get();
    config.process_koid = GetKoid();
    config.watcher = this;
    if (auto status = debug::ZxStatus(
            loop->WatchProcessExceptions(std::move(config), &process_watch_handle_));
        status.has_error()) {
      return status;
    }

    // Check and set ZX_PROP_PROCESS_BREAK_ON_LOAD.
    uintptr_t break_on_load;
    zx_status_t status =
        process_.get_property(ZX_PROP_PROCESS_BREAK_ON_LOAD, &break_on_load, sizeof(break_on_load));
    FX_CHECK(status == ZX_OK);
    // This check should never fail because the debug exception channel obtained above is exclusive.
    FX_CHECK(break_on_load == 0);
    break_on_load = 1;
    status =
        process_.set_property(ZX_PROP_PROCESS_BREAK_ON_LOAD, &break_on_load, sizeof(break_on_load));
    FX_CHECK(status == ZX_OK);
  }
  return debug::Status();
}

void ZirconProcessHandle::Detach() {
  observer_ = nullptr;

  // Unset ZX_PROP_PROCESS_BREAK_ON_LOAD.
  uintptr_t break_on_load = 0;
  FX_CHECK(process_.set_property(ZX_PROP_PROCESS_BREAK_ON_LOAD, &break_on_load,
                                 sizeof(break_on_load)) == ZX_OK);

  // Unbind from the exception port.
  process_watch_handle_.StopWatching();
}

uint64_t ZirconProcessHandle::GetLoaderBreakpointAddress() {
  uintptr_t break_on_load;
  FX_CHECK(process_.get_property(ZX_PROP_PROCESS_BREAK_ON_LOAD, &break_on_load,
                                 sizeof(break_on_load)) == ZX_OK);
  return break_on_load;
}

std::vector<debug_ipc::AddressRegion> ZirconProcessHandle::GetAddressSpace(uint64_t address) const {
  std::vector<debug_ipc::AddressRegion> regions;
  std::vector<zx_info_maps_t> map = GetMaps();

  if (address) {
    // Get a specific region.
    for (const auto& entry : map) {
      if (address < entry.base)
        continue;
      if (address <= (entry.base + entry.size))
        regions.push_back(MapToAddressRegion(entry));
    }
  } else {
    // Get all regions.
    size_t ix = 0;
    regions.resize(map.size());
    for (const auto& entry : map) {
      regions[ix] = MapToAddressRegion(entry);
      ++ix;
    }
  }

  return regions;
}

std::vector<debug_ipc::Module> ZirconProcessHandle::GetModules() const {
  uintptr_t dl_debug_addr;
  FX_CHECK(process_.get_property(ZX_PROP_PROCESS_DEBUG_ADDR, &dl_debug_addr,
                                 sizeof(dl_debug_addr)) == ZX_OK);
  return GetElfModulesForProcess(*this, dl_debug_addr);
}

fit::result<debug::Status, std::vector<debug_ipc::InfoHandle>> ZirconProcessHandle::GetHandles()
    const {
  // Query the handle table size.
  size_t handles_actual = 0;
  size_t handles_avail = 0;
  if (zx_status_t status =
          process_.get_info(ZX_INFO_HANDLE_TABLE, nullptr, 0, &handles_actual, &handles_avail);
      status != ZX_OK)
    return fit::error(debug::ZxStatus(status));

  // We're technically racing with the program, so add some extra buffer in case the process has
  // opened more handles since the above query.
  handles_avail += 64;

  // Read the extended handle table.
  std::vector<zx_info_handle_extended_t> handles(handles_avail);
  if (zx_status_t status = process_.get_info(ZX_INFO_HANDLE_TABLE, handles.data(),
                                             handles_avail * sizeof(zx_info_handle_extended_t),
                                             &handles_actual, &handles_avail);
      status != ZX_OK)
    return fit::error(debug::ZxStatus(status));
  handles.resize(handles_actual);

  // Query the VMO table size.
  size_t vmo_actual = 0;
  size_t vmo_avail = 0;
  if (zx_status_t status =
          process_.get_info(ZX_INFO_PROCESS_VMOS, nullptr, 0, &vmo_actual, &vmo_avail);
      status != ZX_OK)
    return fit::error(debug::ZxStatus(status));
  vmo_avail += 64;  // Try to prevent races as above.

  // Read the VMO table.
  std::vector<zx_info_vmo_t> vmos(vmo_avail);
  if (zx_status_t status =
          process_.get_info(ZX_INFO_PROCESS_VMOS, vmos.data(), vmo_avail * sizeof(zx_info_vmo_t),
                            &vmo_actual, &vmo_avail);
      status != ZX_OK)
    return fit::error(debug::ZxStatus(status));
  vmos.resize(vmo_actual);

  // Index VMOs by koid to allow merging below.
  std::map<zx_koid_t, zx_info_vmo_t> vmo_index;
  for (const auto& vmo : vmos)
    vmo_index[vmo.koid] = vmo;

  std::vector<debug_ipc::InfoHandle> result(handles.size());
  for (size_t i = 0; i < handles.size(); ++i) {
    result[i].type = handles[i].type;
    result[i].handle_value = handles[i].handle_value;
    result[i].rights = handles[i].rights;
    result[i].koid = handles[i].koid;
    result[i].related_koid = handles[i].related_koid;
    result[i].peer_owner_koid = handles[i].peer_owner_koid;

    // VMO-specific extended information.
    if (handles[i].type == ZX_OBJ_TYPE_VMO) {
      if (auto found_vmo = vmo_index.find(handles[i].koid); found_vmo != vmo_index.end()) {
        FillVmoInfo(found_vmo->second, result[i].ext.vmo);

        // Remove VMO info as we find it so we know what wasn't added below.
        vmo_index.erase(found_vmo);
      }
    }
  }

  // Some VMOs won't have open handles. Add these to the table also with 0 handle values. All
  // previously-matched items will have already been removed from the table, so everything left
  // needs to be added.
  for (const auto& [koid, vmo] : vmo_index) {
    auto& dest = result.emplace_back();
    dest.type = ZX_OBJ_TYPE_VMO;
    dest.rights = vmo.handle_rights;
    dest.koid = koid;
    FillVmoInfo(vmo, dest.ext.vmo);
  }

  return fit::success(std::move(result));
}

debug::Status ZirconProcessHandle::ReadMemory(uintptr_t address, void* buffer, size_t len,
                                              size_t* actual) const {
  return debug::ZxStatus(process_.read_memory(address, buffer, len, actual));
}

debug::Status ZirconProcessHandle::WriteMemory(uintptr_t address, const void* buffer, size_t len,
                                               size_t* actual) {
  return debug::ZxStatus(process_.write_memory(address, buffer, len, actual));
}

std::vector<debug_ipc::MemoryBlock> ZirconProcessHandle::ReadMemoryBlocks(uint64_t address,
                                                                          uint32_t size) const {
  // Optimistically assume the read will work which will be faster in the common case.
  if (debug_ipc::MemoryBlock block = ReadOneMemoryBlock(address, size); block.valid)
    return {std::move(block)};

  // Failure reading, this memory is either not mapped or it may cross mapping boundaries. To solve
  // the multiple boundary problem, get the memory mapping and compute all mapping boundaries in the
  // requested region. Then try to read each of the resulting blocks (which may be valid or
  // invalid).
  //
  // This computed boundaries array will contain all boundaries (including the end address and some
  // duplicates) except the begin address (this will be implicit in the later computation).
  std::vector<uint64_t> boundaries;
  for (const zx_info_maps_t& map : GetMaps()) {
    // The returned maps should be sorted so any mapping region starting past our region means all
    // relevant boundaries have been found.
    if (map.base > address + size)
      break;
    if (map.base > address)
      boundaries.push_back(map.base);
    uint64_t end = map.base + map.size;
    if (end > address && end < address + size)
      boundaries.push_back(end);
  }
  boundaries.push_back(address + size);
  std::sort(boundaries.begin(), boundaries.end());

  std::vector<debug_ipc::MemoryBlock> blocks;

  uint64_t begin = address;
  for (uint64_t end : boundaries) {
    // There will be some duplicates in the boundaries array so skip anything that's empty. These
    // duplicates are caused by a range which a child inside it that is coincident with one of the
    // parent boundaries, or two regions that abut each other.
    if (end == begin)
      continue;
    blocks.push_back(ReadOneMemoryBlock(begin, static_cast<uint32_t>(end - begin)));
    begin = end;
  }
  return blocks;
}

debug::Status ZirconProcessHandle::SaveMinidump(const std::vector<DebuggedThread*>& threads,
                                                std::vector<uint8_t>* core_data) {
  debug::Status status = {};

  // Suspend the process while we capture the snapshot.
  crashpad::ScopedTaskSuspend suspend(process_);

  crashpad::ProcessSnapshotFuchsia process_snapshot;
  if (!process_snapshot.Initialize(process_)) {
    status = debug::Status("Failed to initialize minidump from process " +
                           std::to_string(process_koid_) + ".");
    return status;
  }

  // Add any exceptions to the snapshot, if present. This is particularly useful for saving the
  // complete state of a process that was caught in limbo.
  for (const auto& thread : threads) {
    if (thread->in_exception()) {
      zx_exception_report_t exception_report;
      zx_status_t status = thread->thread_handle().GetNativeHandle().get_info(
          ZX_INFO_THREAD_EXCEPTION_REPORT, &exception_report, sizeof(exception_report), nullptr,
          nullptr);
      if (status != ZX_OK) {
        DEBUG_LOG(Process) << "Failed to get ZX_INFO_THREAD_EXCEPTION_REPORT for thread "
                           << thread->koid();
      } else if (!process_snapshot.InitializeException(thread->koid(), exception_report)) {
        DEBUG_LOG(Process) << "Failed to add thread exception report to process snapshot.";
      }
    }
  }

  crashpad::MinidumpFileWriter writer;
  writer.InitializeFromSnapshot(&process_snapshot);

  crashpad::StringFile file;
  if (!writer.WriteEverything(&file)) {
    status = debug::Status("Failed to write core.");
    return status;
  }

  // Copy data out of in memory file to the IPC format.
  const std::string& s = file.string();
  core_data->reserve(s.size());
  std::copy(s.begin(), s.end(), std::back_inserter(*core_data));

  return status;
}

debug_ipc::MemoryBlock ZirconProcessHandle::ReadOneMemoryBlock(uint64_t address,
                                                               uint32_t size) const {
  debug_ipc::MemoryBlock block;

  block.address = address;
  block.size = size;
  block.data.resize(size);

  size_t bytes_read = 0;
  if (process_.read_memory(address, block.data.data(), block.size, &bytes_read) == ZX_OK &&
      bytes_read == size) {
    block.valid = true;
  } else {
    block.valid = false;
    block.data.resize(0);
  }
  return block;
}

std::vector<zx_info_maps_t> ZirconProcessHandle::GetMaps() const {
  const size_t kRegionsCountGuess = 64u;
  const size_t kNewRegionsCountGuess = 4u;

  size_t count_guess = kRegionsCountGuess;

  std::vector<zx_info_maps_t> map;
  size_t actual;
  size_t avail;

  while (true) {
    map.resize(count_guess);

    zx_status_t status = process_.get_info(ZX_INFO_PROCESS_MAPS, map.data(),
                                           sizeof(zx_info_maps) * map.size(), &actual, &avail);

    if (status != ZX_OK) {
      actual = 0;
      break;
    } else if (actual == avail) {
      break;
    }

    count_guess = avail + kNewRegionsCountGuess;
  }

  map.resize(actual);
  return map;
}

void ZirconProcessHandle::OnProcessTerminated(zx_koid_t process_koid) {
  FX_DCHECK(observer_);
  FX_DCHECK(process_koid == GetKoid());
  observer_->OnProcessTerminated();
}

void ZirconProcessHandle::OnThreadStarting(zx::exception exception, zx_exception_info_t info) {
  FX_DCHECK(observer_);
  observer_->OnThreadStarting(std::make_unique<ZirconExceptionHandle>(std::move(exception), info));
}

void ZirconProcessHandle::OnThreadExiting(zx::exception exception, zx_exception_info_t info) {
  FX_DCHECK(observer_);
  observer_->OnThreadExiting(std::make_unique<ZirconExceptionHandle>(std::move(exception), info));
}

void ZirconProcessHandle::OnException(zx::exception exception, zx_exception_info_t info) {
  FX_DCHECK(observer_);
  observer_->OnException(std::make_unique<ZirconExceptionHandle>(std::move(exception), info));
}

}  // namespace debug_agent

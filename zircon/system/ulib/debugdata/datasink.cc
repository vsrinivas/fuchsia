// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/debugdata/datasink.h>
#include <lib/fzl/vmo-mapper.h>
#include <sys/stat.h>
#include <zircon/status.h>

#include <cstddef>
#include <cstdio>
#include <forward_list>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <fbl/string.h>
#include <fbl/unique_fd.h>

#include "src/lib/fxl/strings/string_printf.h"

#include <profile/InstrProfData.inc>

namespace debugdata {

namespace {

constexpr char kProfileSink[] = "llvm-profile";

using IntPtrT = intptr_t;

enum ValueKind {
#define VALUE_PROF_KIND(Enumerator, Value, Descr) Enumerator = (Value),
#include <profile/InstrProfData.inc>
};

struct __llvm_profile_data {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) Type Name;
#include <profile/InstrProfData.inc>
};

struct __llvm_profile_header {
#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer) Type Name;
#include <profile/InstrProfData.inc>
};

std::error_code ReadFile(const fbl::unique_fd& fd, uint8_t* data, size_t size) {
  auto* buf = data;
  ssize_t count = size;
  off_t off = 0;
  while (count > 0) {
    ssize_t len = pread(fd.get(), buf, count, off);
    if (len <= 0) {
      return std::error_code{errno, std::generic_category()};
    }
    buf += len;
    count -= len;
    off += len;
  }
  return std::error_code{};
}

std::error_code WriteFile(const fbl::unique_fd& fd, const uint8_t* data, size_t size) {
  auto* buf = data;
  ssize_t count = size;
  off_t off = 0;
  while (count > 0) {
    ssize_t len = pwrite(fd.get(), buf, count, off);
    if (len <= 0) {
      return std::error_code{errno, std::generic_category()};
    }
    buf += len;
    count -= len;
    off += len;
  }
  return std::error_code{};
}

std::optional<std::string> GetVMOName(const zx::vmo& vmo) {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status = vmo.get_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK || name[0] == '\0') {
    zx_info_handle_basic_t info;
    status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return {};
    }
    snprintf(name, sizeof(name), "unnamed.%" PRIu64, info.koid);
  }
  return name;
}

fbl::String JoinPath(std::string_view parent, std::string_view child) {
  if (parent.empty()) {
    return fbl::String(child);
  }
  if (child.empty()) {
    return fbl::String(parent);
  }
  if (parent[parent.size() - 1] != '/' && child[0] != '/') {
    return fbl::String::Concat({parent, "/", child});
  }
  if (parent[parent.size() - 1] == '/' && child[0] == '/') {
    return fbl::String::Concat({parent, &child[1]});
  }
  return fbl::String::Concat({parent, child});
}

// Returns true if raw profiles |src| and |dst| are structurally compatible.
bool ProfilesCompatible(const uint8_t* dst, const uint8_t* src, size_t size) {
  const __llvm_profile_header* src_header = reinterpret_cast<const __llvm_profile_header*>(src);
  const __llvm_profile_header* dst_header = reinterpret_cast<const __llvm_profile_header*>(dst);

  if (src_header->Magic != dst_header->Magic || src_header->Version != dst_header->Version ||
      src_header->DataSize != dst_header->DataSize ||
      src_header->CountersSize != dst_header->CountersSize ||
      src_header->NamesSize != dst_header->NamesSize)
    return false;

  const __llvm_profile_data* src_data_start =
      reinterpret_cast<const __llvm_profile_data*>(src + sizeof(*src_header));
#if INSTR_PROF_RAW_VERSION > 5
  if (src_header->Version > 5)
    src_data_start = reinterpret_cast<const __llvm_profile_data*>(
        reinterpret_cast<const uint8_t*>(src_data_start) + src_header->BinaryIdsSize);
#endif
  const __llvm_profile_data* src_data_end = src_data_start + src_header->DataSize;
  const __llvm_profile_data* dst_data_start =
      reinterpret_cast<const __llvm_profile_data*>(dst + sizeof(*dst_header));
#if INSTR_PROF_RAW_VERSION > 5
  if (dst_header->Version > 5)
    dst_data_start = reinterpret_cast<const __llvm_profile_data*>(
        reinterpret_cast<const uint8_t*>(dst_data_start) + dst_header->BinaryIdsSize);
#endif
  const __llvm_profile_data* dst_data_end = dst_data_start + dst_header->DataSize;

  for (const __llvm_profile_data *src_data = src_data_start, *dst_data = dst_data_start;
       src_data < src_data_end && dst_data < dst_data_end; ++src_data, ++dst_data) {
    if (src_data->NameRef != dst_data->NameRef || src_data->FuncHash != dst_data->FuncHash ||
        src_data->NumCounters != dst_data->NumCounters)
      return false;
  }

  return true;
}

// Merges raw profiles |src| and |dst| into |dst|.
//
// Note that this function does not check whether the profiles are compatible.
uint8_t* MergeProfiles(uint8_t* dst, const uint8_t* src, size_t size) {
  const __llvm_profile_header* src_header = reinterpret_cast<const __llvm_profile_header*>(src);
  const __llvm_profile_data* src_data_start =
      reinterpret_cast<const __llvm_profile_data*>(src + sizeof(*src_header));
#if INSTR_PROF_RAW_VERSION > 5
  if (src_header->Version > 5)
    src_data_start = reinterpret_cast<const __llvm_profile_data*>(
        reinterpret_cast<const uint8_t*>(src_data_start) + src_header->BinaryIdsSize);
#endif
  const __llvm_profile_data* src_data_end = src_data_start + src_header->DataSize;
  const uint64_t* src_counters_start = reinterpret_cast<const uint64_t*>(src_data_end);
  uintptr_t src_counters_delta = src_header->CountersDelta;

  __llvm_profile_header* dst_header = reinterpret_cast<__llvm_profile_header*>(dst);
  __llvm_profile_data* dst_data_start =
      reinterpret_cast<__llvm_profile_data*>(dst + sizeof(*dst_header));
#if INSTR_PROF_RAW_VERSION > 5
  if (dst_header->Version > 5)
    dst_data_start = reinterpret_cast<__llvm_profile_data*>(
        reinterpret_cast<uint8_t*>(dst_data_start) + dst_header->BinaryIdsSize);
#endif
  __llvm_profile_data* dst_data_end = dst_data_start + dst_header->DataSize;
  uint64_t* dst_counters_start = reinterpret_cast<uint64_t*>(dst_data_end);
  uintptr_t dst_counters_delta = dst_header->CountersDelta;

  const __llvm_profile_data* src_data = src_data_start;
  __llvm_profile_data* dst_data = dst_data_start;
  for (; src_data < src_data_end && dst_data < dst_data_end; src_data++, dst_data++) {
    const uint64_t* src_counters =
        src_counters_start + (src_data->CounterPtr - src_counters_delta) / sizeof(uint64_t);
    if (src_header->Version >= 7)
      src_counters_delta -= sizeof(*src_data);
    uint64_t* dst_counters =
        dst_counters_start + (dst_data->CounterPtr - dst_counters_delta) / sizeof(uint64_t);
    if (src_header->Version >= 7)
      dst_counters_delta -= sizeof(*dst_data);
    for (unsigned i = 0; i < src_data->NumCounters; i++) {
      dst_counters[i] += src_counters[i];
    }
  }

  return dst;
}

// Process all data sink dumps and write to the disk.
std::optional<DumpFile> ProcessDataSinkDump(const std::string& sink_name, const zx::vmo& file_data,
                                            const fbl::unique_fd& data_sink_dir_fd,
                                            DataSinkCallback& error_callback,
                                            DataSinkCallback& warning_callback) {
  zx_status_t status;

  if (mkdirat(data_sink_dir_fd.get(), sink_name.c_str(), 0777) != 0 && errno != EEXIST) {
    error_callback(fxl::StringPrintf("FAILURE: cannot mkdir \"%s\" for data-sink: %s\n",
                                     sink_name.c_str(), strerror(errno)));
    return {};
  }
  fbl::unique_fd sink_dir_fd{
      openat(data_sink_dir_fd.get(), sink_name.c_str(), O_RDONLY | O_DIRECTORY)};
  if (!sink_dir_fd) {
    error_callback(fxl::StringPrintf("FAILURE: cannot open data-sink directory \"%s\": %s\n",
                                     sink_name.c_str(), strerror(errno)));
    return {};
  }

  auto name = GetVMOName(file_data);
  if (!name) {
    error_callback(fxl::StringPrintf("FAILURE: Cannot get a name for the VMO\n"));
    return {};
  }

  uint64_t size;
  status = file_data.get_size(&size);
  if (status != ZX_OK) {
    error_callback(
        fxl::StringPrintf("FAILURE: Cannot get size of VMO \"%s\" for data-sink \"%s\": %s\n",
                          name->c_str(), sink_name.c_str(), zx_status_get_string(status)));
    return {};
  }

  fzl::VmoMapper mapper;
  if (size > 0) {
    zx_status_t status = mapper.Map(file_data, 0, size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      error_callback(fxl::StringPrintf("FAILURE: Cannot map VMO \"%s\" for data-sink \"%s\": %s\n",
                                       name->c_str(), sink_name.c_str(),
                                       zx_status_get_string(status)));
      return {};
    }
  } else {
    warning_callback(fxl::StringPrintf("WARNING: Empty VMO \"%s\" published for data-sink \"%s\"\n",
                                       name->c_str(), sink_name.c_str()));
    return {};
  }

  zx_info_handle_basic_t info;
  status = file_data.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    error_callback(fxl::StringPrintf("FAILURE: Cannot get a basic info for VMO \"%s\": %s\n",
                                     name->c_str(), zx_status_get_string(status)));
    return {};
  }

  char filename[ZX_MAX_NAME_LEN];
  snprintf(filename, sizeof(filename), "%s.%" PRIu64, sink_name.c_str(), info.koid);
  fbl::unique_fd fd{openat(sink_dir_fd.get(), filename, O_WRONLY | O_CREAT | O_EXCL, 0666)};
  if (!fd) {
    error_callback(fxl::StringPrintf("FAILURE: Cannot open data-sink file \"%s\": %s\n", filename,
                                     strerror(errno)));
    return {};
  }
  if (std::error_code ec = WriteFile(fd, reinterpret_cast<uint8_t*>(mapper.start()), size); ec) {
    error_callback(fxl::StringPrintf("FAILURE: Cannot write data to \"%s\": %s\n", filename,
                                     strerror(ec.value())));
    return {};
  }

  return DumpFile{*name, JoinPath(sink_name, filename).c_str()};
}

}  // namespace

void DataSink::ProcessSingleDebugData(const std::string& data_sink, zx::vmo debug_data,
                                      DataSinkCallback& error_callback,
                                      DataSinkCallback& warning_callback) {
  if (data_sink == kProfileSink) {
    ProcessProfile(debug_data, error_callback, warning_callback);
  } else {
    auto dump_file = ProcessDataSinkDump(data_sink, debug_data, data_sink_dir_fd_, error_callback,
                                         warning_callback);
    if (dump_file) {
      dump_files_[data_sink].emplace(*dump_file);
    }
  }
}

DataSinkFileMap DataSink::FlushToDirectory(DataSinkCallback& error_callback,
                                           DataSinkCallback& warning_callback) {
  if (mkdirat(data_sink_dir_fd_.get(), kProfileSink, 0777) != 0 && errno != EEXIST) {
    error_callback(fxl::StringPrintf("FAILURE: cannot mkdir \"%s\" for data-sink: %s\n",
                                     kProfileSink, strerror(errno)));
    return {};
  }
  fbl::unique_fd sink_dir_fd{openat(data_sink_dir_fd_.get(), kProfileSink, O_RDONLY | O_DIRECTORY)};
  if (!sink_dir_fd) {
    error_callback(fxl::StringPrintf("FAILURE: cannot open data-sink directory \"%s\": %s\n",
                                     kProfileSink, strerror(errno)));
    return {};
  }

  for (auto& [name, buffer_and_size] : merged_profiles_) {
    auto& [buffer, buffer_size] = buffer_and_size;
    fbl::unique_fd fd{openat(sink_dir_fd.get(), name.c_str(), O_RDWR | O_CREAT, 0666)};
    if (!fd) {
      error_callback(fxl::StringPrintf("FAILURE: Cannot open data-sink file \"%s\": %s\n",
                                       name.c_str(), strerror(errno)));
      return {};
    }
    struct stat stat;
    if (fstat(fd.get(), &stat) == -1) {
      error_callback(fxl::StringPrintf("FAILURE: Cannot stat data-sink file \"%s\": %s\n",
                                       name.c_str(), strerror(errno)));
      return {};
    }
    if (auto file_size = static_cast<uint64_t>(stat.st_size); file_size > 0) {
      // The file already exists. Merge with buffer and write back.
      std::unique_ptr<uint8_t[]> file_buffer = std::make_unique<uint8_t[]>(file_size);
      if (std::error_code ec = ReadFile(fd, file_buffer.get(), file_size); ec) {
        error_callback(fxl::StringPrintf("FAILURE: Cannot read data from \"%s\": %s\n",
                                         name.c_str(), strerror(ec.value())));
        return {};
      }
      if (buffer_size != file_size) {
        error_callback(
            fxl::StringPrintf("FAILURE: Mismatch between content sizes for \"%s\": %lu != %lu\n",
                              name.c_str(), buffer_size, file_size));
      }
      ZX_ASSERT(buffer_size == file_size);

      // Ensure that profiles are structuraly compatible.
      if (!ProfilesCompatible(buffer.get(), file_buffer.get(), buffer_size)) {
        warning_callback(fxl::StringPrintf("WARNING: Unable to merge profile data: %s\n",
                                           "source profile file is not compatible"));
        continue;
      }
      MergeProfiles(buffer.get(), file_buffer.get(), file_size);
    }

    if (std::error_code ec = WriteFile(fd, buffer.get(), buffer_size); ec) {
      error_callback(fxl::StringPrintf("FAILURE: Cannot write data to \"%s\": %s\n", name.c_str(),
                                       strerror(ec.value())));
      return {};
    }
    dump_files_[kProfileSink].emplace(DumpFile{name, JoinPath(kProfileSink, name).c_str()});
  }
  std::unordered_map<std::string, std::unordered_set<DumpFile, HashDumpFile, EqDumpFile>> result;
  std::swap(result, dump_files_);
  return result;
}

// This function processes all raw profiles that were published via data sink
// in an efficient manner. It merges all profiles from the same binary into a single
// profile. First it groups all VMOs by name which uniquely identifies each
// binary. Then it merges together all VMOs for the same binary. This ensures that at the
// end, we have exactly one profile for each binary in total.
void DataSink::ProcessProfile(const zx::vmo& vmo, DataSinkCallback& error_callback,
                              DataSinkCallback& warning_callback) {
  // Group data by profile name. The name is a hash computed from profile metadata and
  // should be unique across all binaries (modulo hash collisions).
  auto optional_name = GetVMOName(vmo);
  if (!optional_name) {
    error_callback(fxl::StringPrintf("FAILURE: Cannot get a name for the VMO\n"));
    return;
  }
  std::string name = *optional_name;

  zx_status_t status;
  uint64_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    error_callback(
        fxl::StringPrintf("FAILURE: Cannot get size of VMO \"%s\" for data-sink \"%s\": %s\n",
                          name.c_str(), kProfileSink, zx_status_get_string(status)));
    return;
  }

  fzl::VmoMapper mapper;
  if (vmo_size > 0) {
    zx_status_t status = mapper.Map(vmo, 0, vmo_size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      error_callback(fxl::StringPrintf("FAILURE: Cannot map VMO \"%s\" for data-sink \"%s\": %s\n",
                                       name.c_str(), kProfileSink, zx_status_get_string(status)));
      return;
    }
  } else {
    warning_callback(fxl::StringPrintf("WARNING: Empty VMO \"%s\" published for data-sink \"%s\"\n",
                                       kProfileSink, name.c_str()));
    return;
  }

  auto existing_buffer_and_size = merged_profiles_.find(name);
  if (existing_buffer_and_size == merged_profiles_.end()) {
    // new profile name, create a new buffer
    std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(vmo_size);
    memcpy(buffer.get(), mapper.start(), vmo_size);

    merged_profiles_.emplace(name, std::make_pair(std::move(buffer), vmo_size));
  } else {
    // profile name exists, merge with existing
    auto& [buffer, buffer_size] = existing_buffer_and_size->second;

    if (buffer_size != vmo_size) {
      error_callback(
          fxl::StringPrintf("FAILURE: Mismatch between content sizes for \"%s\": %lu != %lu\n",
                            name.c_str(), buffer_size, vmo_size));
    }
    ZX_ASSERT(buffer_size == vmo_size);

    // Ensure that profiles are structuraly compatible.
    if (!ProfilesCompatible(buffer.get(), reinterpret_cast<const uint8_t*>(mapper.start()),
                            buffer_size)) {
      warning_callback(fxl::StringPrintf("WARNING: Unable to merge profile data: %s\n",
                                         "source profile file is not compatible"));
      return;
    }

    MergeProfiles(buffer.get(), reinterpret_cast<const uint8_t*>(mapper.start()), buffer_size);
  }
}

DataSinkFileMap ProcessDebugData(const fbl::unique_fd& data_sink_dir_fd,
                                 std::unordered_map<std::string, std::vector<zx::vmo>> debug_data,
                                 DataSinkCallback error_callback,
                                 DataSinkCallback warning_callback) {
  DataSink data_sink(data_sink_dir_fd);
  for (auto& [data_sink_name, vmos] : debug_data) {
    for (auto& vmo : vmos) {
      data_sink.ProcessSingleDebugData(data_sink_name, std::move(vmo), error_callback,
                                       warning_callback);
    }
  }
  return data_sink.FlushToDirectory(error_callback, warning_callback);
}

}  // namespace debugdata

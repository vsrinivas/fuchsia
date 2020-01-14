// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/inference.h"

#include <ios>
#include <ostream>

#include "src/lib/fidl_codec/printer.h"
#include "tools/fidlcat/lib/syscall_decoder.h"

namespace fidlcat {

std::string_view HandleDescription::Convert(uint32_t type) {
  switch (type) {
    case PA_PROC_SELF:
      return "proc-self";
    case PA_THREAD_SELF:
      return "thread-self";
    case PA_JOB_DEFAULT:
      return "job-default";
    case PA_VMAR_ROOT:
      return "vmar-root";
    case PA_VMAR_LOADED:
      return "initial-program-image-vmar";
    case PA_LDSVC_LOADER:
      return "ldsvc-loader";
    case PA_VMO_VDSO:
      return "vdso-vmo";
    case PA_VMO_STACK:
      return "stack-vmo";
    case PA_VMO_EXECUTABLE:
      return "executable-vmo";
    case PA_VMO_BOOTDATA:
      return "bootdata-vmo";
    case PA_VMO_BOOTFS:
      return "bootfs-vmo";
    case PA_VMO_KERNEL_FILE:
      return "kernel-file-vmo";
    case PA_NS_DIR:
      return "dir";
    case PA_FD:
      return "fd";
    case PA_DIRECTORY_REQUEST:
      return "directory-request";
    case PA_RESOURCE:
      return "resource";
    case PA_USER0:
      return "user0";
    case PA_USER1:
      return "user1";
    case PA_USER2:
      return "user2";
    default:
      return "";
  }
}

void HandleDescription::Display(const fidl_codec::Colors& colors, std::ostream& os) const {
  if (!type_.empty()) {
    os << colors.green << type_ << colors.reset;
    if (fd_ != -1) {
      os << ':' << colors.blue << fd_ << colors.reset;
    }
    if (!path_.empty()) {
      os << ':' << colors.blue << path_ << colors.reset;
    }
  }
}

// This is the first function which is intercepted. This gives us information about
// all the handles an application have at startup. However, for directory handles,
// we don't have the name of the directory.
void Inference::ExtractHandles(SyscallDecoder* decoder) {
  constexpr int kNhandles = 0;
  constexpr int kHandles = 1;
  constexpr int kHandleInfo = 2;
  // Get the values which have been harvest by the debugger using they argument number.
  uint32_t nhandles = decoder->ArgumentValue(kNhandles);
  const zx_handle_t* handles =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandles));
  const uint32_t* handle_info =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandleInfo));
  // Get the information about all the handles.
  // The meaning of handle info is described in zircon/system/public/zircon/processargs.h
  for (uint32_t handle = 0; handle < nhandles; ++handle) {
    if (handles[handle] != 0) {
      uint32_t type = PA_HND_TYPE(handle_info[handle]);
      switch (type) {
        case PA_FD:
          AddHandleDescription(handles[handle], "fd", PA_HND_ARG(handle_info[handle]));
          break;
        case PA_DIRECTORY_REQUEST:
          AddHandleDescription(handles[handle], "directory-request", "/");
          break;
        default:
          AddHandleDescription(handles[handle], type);
          break;
      }
    }
  }
}

// This is the second function which is intercepted. This gives us information about
// all the handles which have not been used by processargs_extract_handles.
// This only adds information about directories.
void Inference::LibcExtensionsInit(SyscallDecoder* decoder) {
  constexpr int kHandleCount = 0;
  constexpr int kHandles = 1;
  constexpr int kHandleInfo = 2;
  constexpr int kNameCount = 3;
  constexpr int kNames = 4;
  // Get the values which have been harvest by the debugger using they argument number.
  uint32_t handle_count = decoder->ArgumentValue(kHandleCount);
  const zx_handle_t* handles =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandles));
  const uint32_t* handle_info =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandleInfo));
  uint32_t name_count = decoder->ArgumentValue(kNameCount);
  const uint64_t* names =
      reinterpret_cast<const uint64_t*>(decoder->ArgumentContent(Stage::kEntry, kNames));
  // Get the information about the remaining handles.
  // The meaning of handle info is described in zircon/system/public/zircon/processargs.h
  for (uint32_t handle = 0; handle < handle_count; ++handle) {
    if (handles[handle] != 0) {
      uint32_t type = PA_HND_TYPE(handle_info[handle]);
      switch (type) {
        case PA_NS_DIR: {
          uint32_t index = PA_HND_ARG(handle_info[handle]);
          AddHandleDescription(handles[handle], "dir",
                               (index < name_count)
                                   ? reinterpret_cast<const char*>(
                                         decoder->BufferContent(Stage::kEntry, names[index]))
                                   : "");
          break;
        }
        case PA_FD:
          AddHandleDescription(handles[handle], "fd", PA_HND_ARG(handle_info[handle]));
          break;
        case PA_DIRECTORY_REQUEST:
          AddHandleDescription(handles[handle], "directory-request", "/");
          break;
        default:
          AddHandleDescription(handles[handle], type);
          break;
      }
    }
  }
}

}  // namespace fidlcat

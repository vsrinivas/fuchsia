// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <cctype>
#include <cinttypes>

#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_printf.h"

#include "byte_block.h"

namespace debugger_utils {

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                   sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

zx_koid_t GetKoid(const zx::object_base& object) {
  return GetKoid(object.get());
}

zx_koid_t GetRelatedKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                   sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }
  return info.related_koid;
}

zx_koid_t GetRelatedKoid(const zx::object_base& object) {
  return GetRelatedKoid(object.get());
}

std::string GetObjectName(zx_handle_t handle) {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status = zx_object_get_property(handle, ZX_PROP_NAME,
                                              name, sizeof(name));
  if (status < 0) {
    return "";
  }
  return name;
}

std::string GetObjectName(const zx::object_base& object) {
  return GetObjectName(object.get());
}

std::string ZxErrorString(zx_status_t status) {
  return fxl::StringPrintf("%s(%d)", zx_status_get_string(status), status);
}

const char* ExceptionName(uint32_t type) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (type) {
    CASE_TO_STR(ZX_EXCP_GENERAL);
    CASE_TO_STR(ZX_EXCP_FATAL_PAGE_FAULT);
    CASE_TO_STR(ZX_EXCP_UNDEFINED_INSTRUCTION);
    CASE_TO_STR(ZX_EXCP_SW_BREAKPOINT);
    CASE_TO_STR(ZX_EXCP_HW_BREAKPOINT);
    CASE_TO_STR(ZX_EXCP_UNALIGNED_ACCESS);
    CASE_TO_STR(ZX_EXCP_THREAD_STARTING);
    CASE_TO_STR(ZX_EXCP_THREAD_EXITING);
    CASE_TO_STR(ZX_EXCP_POLICY_ERROR);
    default:
      return nullptr;
  }
#undef CASE_TO_STR
}

std::string ExceptionNameAsString(uint32_t type) {
  const char* name = ExceptionName(type);
  if (name) {
    return std::string(name);
  }
  return fxl::StringPrintf("UNKNOWN(0x%x)", type);
}

bool ReadString(const ByteBlock& m, zx_vaddr_t vaddr, char* ptr, size_t max) {
  while (max > 1) {
    if (!m.Read(vaddr, ptr, 1)) {
      *ptr = '\0';
      return false;
    }
    ptr++;
    vaddr++;
    max--;
  }
  *ptr = '\0';
  return true;
}

}  // namespace debugger_utils

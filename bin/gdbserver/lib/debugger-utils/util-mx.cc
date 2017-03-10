// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <cctype>
#include <cinttypes>

#include <magenta/status.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_printf.h"

#include "memory.h"

namespace debugserver {
namespace util {

void LogErrorWithMxStatus(const std::string& message, mx_status_t status) {
  FTL_LOG(ERROR) << message << ": " << mx_status_get_string(status)
                 << " (" << status << ")";
}

const char* ExceptionName(mx_excp_type_t type) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (type) {
    CASE_TO_STR(MX_EXCP_GENERAL);
    CASE_TO_STR(MX_EXCP_FATAL_PAGE_FAULT);
    CASE_TO_STR(MX_EXCP_UNDEFINED_INSTRUCTION);
    CASE_TO_STR(MX_EXCP_SW_BREAKPOINT);
    CASE_TO_STR(MX_EXCP_HW_BREAKPOINT);
    CASE_TO_STR(MX_EXCP_THREAD_STARTING);
    CASE_TO_STR(MX_EXCP_THREAD_EXITING);
    CASE_TO_STR(MX_EXCP_GONE);
    default:
      return "UNKNOWN";
  }
#undef CASE_TO_STR
}

std::string ExceptionToString(mx_excp_type_t type,
                              const mx_exception_context_t& context) {
  std::string result(ExceptionName(type));
  // TODO(dje): Add more info to the string.
  return result;
}

bool ReadString(const Memory& m, mx_vaddr_t vaddr, char* ptr, size_t max) {
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

}  // namespace util
}  // namespace debugserver

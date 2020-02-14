// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic.h"

#include <string>

#include "src/lib/fidl_codec/printer.h"
#include "zircon/system/public/zircon/processargs.h"
#include "zircon/system/public/zircon/types.h"

namespace fidl_codec {
namespace semantic {

void ExpressionRequest::Dump(std::ostream& os) const { os << "request"; }

void ExpressionHandle::Dump(std::ostream& os) const { os << "handle"; }

void ExpressionFieldAccess::Dump(std::ostream& os) const { os << *expression_ << '.' << field_; }

void ExpressionSlash::Dump(std::ostream& os) const { os << *left_ << " / " << *right_; }

void Assignment::Dump(std::ostream& os) const { os << *destination_ << " = " << *source_ << '\n'; }

void MethodSemantic::Dump(std::ostream& os) const {
  for (const auto& assignment : assignments_) {
    assignment->Dump(os);
  }
}

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

}  // namespace semantic
}  // namespace fidl_codec

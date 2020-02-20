// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic.h"

#include <string>

#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "zircon/system/public/zircon/processargs.h"
#include "zircon/system/public/zircon/types.h"

namespace fidl_codec {
namespace semantic {

void ExpressionRequest::Dump(std::ostream& os) const { os << "request"; }

bool ExpressionRequest::Execute(SemanticContext* context, ExpressionValue* result) const {
  if (context->request() == nullptr) {
    return false;
  }
  result->set(context->request());
  return true;
}

void ExpressionHandle::Dump(std::ostream& os) const { os << "handle"; }

bool ExpressionHandle::Execute(SemanticContext* context, ExpressionValue* result) const {
  result->set(context->handle());
  return true;
}

void ExpressionFieldAccess::Dump(std::ostream& os) const { os << *expression_ << '.' << field_; }

bool ExpressionFieldAccess::Execute(SemanticContext* context, ExpressionValue* result) const {
  ExpressionValue value;
  if (!expression_->Execute(context, &value)) {
    return false;
  }
  if (value.value() != nullptr) {
    const StructValue* struct_value = value.value()->AsStructValue();
    if (struct_value != nullptr) {
      const Value* field_value = struct_value->GetFieldValue(field_);
      if (field_value == nullptr) {
        return false;
      }
      result->set(field_value);
      return true;
    }
  }
  return false;
}

void ExpressionSlash::Dump(std::ostream& os) const { os << *left_ << " / " << *right_; }

bool ExpressionSlash::Execute(SemanticContext* context, ExpressionValue* result) const {
  ExpressionValue left_value;
  ExpressionValue right_value;
  if (!left_->Execute(context, &left_value) || !right_->Execute(context, &right_value)) {
    return false;
  }
  const HandleDescription* description = left_value.handle_description();
  if ((description == nullptr) && (left_value.handle() != ZX_HANDLE_INVALID)) {
    description =
        context->handle_semantic()->GetHandleDescription(context->pid(), left_value.handle());
  }
  if (description == nullptr) {
    return false;
  }
  if (right_value.value() != nullptr) {
    auto string_value = right_value.value()->AsStringValue();
    if (string_value != nullptr) {
      if (description->path().empty()) {
        result->set(description->type(), description->fd(), string_value->string());
        return true;
      }
      if (string_value->string() == ".") {
        result->set(description->type(), description->fd(), description->path());
        return true;
      }
      const char* data = string_value->string().c_str();
      size_t length = string_value->string().size();
      if ((length >= 2) && (strncmp(data, "./", 2) == 0)) {
        data += 2;
        length -= 2;
      }
      if (description->path() == "/") {
        result->set(description->type(), description->fd(), "/" + std::string(data, length));
        return true;
      }
      result->set(description->type(), description->fd(),
                  description->path() + "/" + std::string(data, length));
      return true;
    }
  }
  return false;
}

void Assignment::Dump(std::ostream& os) const { os << *destination_ << " = " << *source_ << '\n'; }

void Assignment::Execute(SemanticContext* context) const {
  ExpressionValue destination_value;
  ExpressionValue source_value;
  if (!destination_->Execute(context, &destination_value) ||
      !source_->Execute(context, &source_value)) {
    return;
  }
  if (destination_value.value() == nullptr) {
    return;
  }
  auto handle_value = destination_value.value()->AsHandleValue();
  if (handle_value == nullptr) {
    return;
  }
  zx_handle_t destination_handle = handle_value->handle().handle;
  if (destination_handle == ZX_HANDLE_INVALID) {
    return;
  }
  // Currently we only work on requests. If we also work on response, this would need to be
  // modified.
  switch (context->type()) {
    case ContextType::kRead:
      break;
    case ContextType::kWrite:
    case ContextType::kCall:
      destination_handle =
          context->handle_semantic()->GetLinkedHandle(context->pid(), destination_handle);
      if (destination_handle == ZX_HANDLE_INVALID) {
        return;
      }
      break;
  }
  context->handle_semantic()->AddHandleDescription(context->pid(), destination_handle,
                                                   source_value.handle_description());
}

void MethodSemantic::Dump(std::ostream& os) const {
  for (const auto& assignment : assignments_) {
    assignment->Dump(os);
  }
}

void MethodSemantic::ExecuteAssignments(SemanticContext* context) const {
  for (const auto& assignment : assignments_) {
    assignment->Execute(context);
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

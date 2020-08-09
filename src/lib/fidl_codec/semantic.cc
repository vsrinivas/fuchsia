// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic.h"

#include <zircon/system/public/zircon/processargs.h>
#include <zircon/system/public/zircon/types.h>

#include <string>

#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {
namespace semantic {

void ExpressionStringLiteral::Dump(std::ostream& os) const { os << '\'' << value_ << '\''; }

bool ExpressionStringLiteral::Execute(SemanticContext* context, ExpressionValue* result) const {
  result->set(value_);
  return true;
}

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

void ExpressionHandleDescription::Dump(std::ostream& os) const {
  os << "HandleDescription(" << *type_ << ", " << *path_ << ')';
}

bool ExpressionHandleDescription::Execute(SemanticContext* context, ExpressionValue* result) const {
  ExpressionValue type;
  ExpressionValue path;
  if (!type_->Execute(context, &type) || !path_->Execute(context, &path)) {
    return false;
  }
  if (!type.string() || !path.string()) {
    return false;
  }
  result->set(*type.string(), -1, *path.string(), "");
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
      const StringValue* string = field_value->AsStringValue();
      if (string == nullptr) {
        result->set(field_value);
      } else {
        result->set(string->string());
      }
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
  const InferredHandleInfo* inferred_handle_info = left_value.inferred_handle_info();
  if ((inferred_handle_info == nullptr) && (left_value.handle() != ZX_HANDLE_INVALID)) {
    inferred_handle_info =
        context->handle_semantic()->GetInferredHandleInfo(context->pid(), left_value.handle());
  }
  if (inferred_handle_info == nullptr) {
    return false;
  }
  if (right_value.string()) {
    if (inferred_handle_info->path().empty()) {
      result->set(inferred_handle_info->type(), inferred_handle_info->fd(), *right_value.string(),
                  inferred_handle_info->attributes());
      return true;
    }
    if (*right_value.string() == ".") {
      result->set(inferred_handle_info->type(), inferred_handle_info->fd(),
                  inferred_handle_info->path(), inferred_handle_info->attributes());
      return true;
    }
    std::string path(*right_value.string());
    if (path.find("./") == 0) {
      path.erase(0, 2);
    }
    if (inferred_handle_info->path() == "/") {
      result->set(inferred_handle_info->type(), inferred_handle_info->fd(), "/" + path,
                  inferred_handle_info->attributes());
      return true;
    }
    result->set(inferred_handle_info->type(), inferred_handle_info->fd(),
                inferred_handle_info->path() + "/" + path, inferred_handle_info->attributes());
    return true;
  }
  return false;
}

void ExpressionColon::Dump(std::ostream& os) const { os << *left_ << " : " << *right_; }

bool ExpressionColon::Execute(SemanticContext* context, ExpressionValue* result) const {
  ExpressionValue left_value;
  ExpressionValue right_value;
  if (!left_->Execute(context, &left_value) || !right_->Execute(context, &right_value)) {
    return false;
  }
  const InferredHandleInfo* inferred_handle_info = left_value.inferred_handle_info();
  if ((inferred_handle_info == nullptr) && (left_value.handle() != ZX_HANDLE_INVALID)) {
    inferred_handle_info =
        context->handle_semantic()->GetInferredHandleInfo(context->pid(), left_value.handle());
  }
  if (inferred_handle_info == nullptr) {
    return false;
  }
  if (right_value.string()) {
    if (inferred_handle_info->attributes().empty()) {
      result->set(inferred_handle_info->type(), inferred_handle_info->fd(),
                  inferred_handle_info->path(), *right_value.string());
      return true;
    }
    result->set(inferred_handle_info->type(), inferred_handle_info->fd(),
                inferred_handle_info->path(),
                inferred_handle_info->attributes() + ", " + std::string(*right_value.string()));
    return true;
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
  const InferredHandleInfo* inferred_handle_info = source_value.inferred_handle_info();
  if ((inferred_handle_info == nullptr) && (source_value.handle() != ZX_HANDLE_INVALID)) {
    inferred_handle_info =
        context->handle_semantic()->GetInferredHandleInfo(context->pid(), source_value.handle());
  }
  context->handle_semantic()->CreateHandleInfo(context->tid(), destination_handle);
  context->handle_semantic()->AddInferredHandleInfo(context->pid(), destination_handle,
                                                    inferred_handle_info);
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

std::string_view InferredHandleInfo::Convert(uint32_t type) {
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

void InferredHandleInfo::Display(PrettyPrinter& printer) const {
  if (!type_.empty()) {
    printer << Green << type_ << ResetColor;
    if (fd_ != -1) {
      printer << ':' << Blue << fd_ << ResetColor;
    }
    if (!path_.empty()) {
      printer << ':' << Blue << path_ << ResetColor;
    }
    if (!attributes_.empty()) {
      printer << " [" << Blue << attributes_ << ResetColor << ']';
    }
  }
}

}  // namespace semantic
}  // namespace fidl_codec

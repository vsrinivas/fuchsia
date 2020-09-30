// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic.h"

#include <zircon/processargs.h>
#include <zircon/types.h>

#include <string>

#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {
namespace semantic {

void ExpressionStringLiteral::Dump(std::ostream& os) const { os << '\'' << value_ << '\''; }

bool ExpressionStringLiteral::Execute(SemanticContext* context, ExpressionValue* result) const {
  result->set_string(value_);
  return true;
}

void ExpressionRequest::Dump(std::ostream& os) const { os << "request"; }

bool ExpressionRequest::Execute(SemanticContext* context, ExpressionValue* result) const {
  if (context->request() == nullptr) {
    return false;
  }
  result->set_value(nullptr, context->request());
  return true;
}

void ExpressionHandle::Dump(std::ostream& os) const { os << "handle"; }

bool ExpressionHandle::Execute(SemanticContext* context, ExpressionValue* result) const {
  result->set_handle(context->handle());
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
  if ((type.kind() != ExpressionValue::kString) || (path.kind() != ExpressionValue::kString)) {
    return false;
  }
  result->set_inferred_handle_info(type.string(), -1, path.string(), "");
  return true;
}

void ExpressionFieldAccess::Dump(std::ostream& os) const { os << *expression_ << '.' << field_; }

bool ExpressionFieldAccess::Execute(SemanticContext* context, ExpressionValue* result) const {
  ExpressionValue value;
  if (!expression_->Execute(context, &value)) {
    return false;
  }
  if (value.kind() == ExpressionValue::kValue) {
    const StructValue* struct_value = value.value()->AsStructValue();
    if (struct_value != nullptr) {
      std::pair<const Type*, const Value*> field_value = struct_value->GetFieldValue(field_);
      if (field_value.second == nullptr) {
        return false;
      }
      const StringValue* string = field_value.second->AsStringValue();
      if (string == nullptr) {
        result->set_value(field_value.first, field_value.second);
      } else {
        result->set_string(string->string());
      }
      return true;
    }
    if (field_ == "size") {
      const StringValue* string_value = value.value()->AsStringValue();
      if (string_value != nullptr) {
        result->set_integer(string_value->string().size());
        return true;
      }
      const VectorValue* vector_value = value.value()->AsVectorValue();
      if (vector_value != nullptr) {
        result->set_integer(vector_value->values().size());
        return true;
      }
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
  const InferredHandleInfo* inferred_handle_info = nullptr;
  if (left_value.kind() == ExpressionValue::kInferredHandleInfo) {
    inferred_handle_info = left_value.inferred_handle_info();
  }
  if (left_value.kind() == ExpressionValue::kHandle) {
    inferred_handle_info =
        context->handle_semantic()->GetInferredHandleInfo(context->pid(), left_value.handle());
  }
  if (inferred_handle_info == nullptr) {
    return false;
  }
  if (right_value.kind() == ExpressionValue::kString) {
    if (inferred_handle_info->path().empty()) {
      result->set_inferred_handle_info(inferred_handle_info->type(), inferred_handle_info->fd(),
                                       right_value.string(), inferred_handle_info->attributes());
      return true;
    }
    if (right_value.string() == ".") {
      result->set_inferred_handle_info(inferred_handle_info->type(), inferred_handle_info->fd(),
                                       inferred_handle_info->path(),
                                       inferred_handle_info->attributes());
      return true;
    }
    std::string path(right_value.string());
    if (path.find("./") == 0) {
      path.erase(0, 2);
    }
    if (inferred_handle_info->path() == "/") {
      result->set_inferred_handle_info(inferred_handle_info->type(), inferred_handle_info->fd(),
                                       "/" + path, inferred_handle_info->attributes());
      return true;
    }
    result->set_inferred_handle_info(inferred_handle_info->type(), inferred_handle_info->fd(),
                                     inferred_handle_info->path() + "/" + path,
                                     inferred_handle_info->attributes());
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
  const InferredHandleInfo* inferred_handle_info = nullptr;
  if (left_value.kind() == ExpressionValue::kInferredHandleInfo) {
    inferred_handle_info = left_value.inferred_handle_info();
  }
  if (left_value.kind() == ExpressionValue::kHandle) {
    inferred_handle_info =
        context->handle_semantic()->GetInferredHandleInfo(context->pid(), left_value.handle());
  }
  if (inferred_handle_info == nullptr) {
    return false;
  }
  if (right_value.kind() == ExpressionValue::kString) {
    if (inferred_handle_info->attributes().empty()) {
      result->set_inferred_handle_info(inferred_handle_info->type(), inferred_handle_info->fd(),
                                       inferred_handle_info->path(), right_value.string());
      return true;
    }
    result->set_inferred_handle_info(
        inferred_handle_info->type(), inferred_handle_info->fd(), inferred_handle_info->path(),
        inferred_handle_info->attributes() + ", " + std::string(right_value.string()));
    return true;
  }
  return false;
}

void Assignment::Dump(std::ostream& os) const { os << *destination_ << " = " << *source_ << '\n'; }

void Assignment::Execute(AssignmentSemanticContext* context) const {
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

void MethodSemantic::ExecuteAssignments(AssignmentSemanticContext* context) const {
  for (const auto& assignment : assignments_) {
    assignment->Execute(context);
  }
}

void DisplayExpression::Dump(std::ostream& os) const {
  if (!header_.empty()) {
    os << " \"" << header_ << '"';
  }
  if (expression_ != nullptr) {
    os << ' ';
    expression_->Dump(os);
  }
  if (!footer_.empty()) {
    os << " \"" << footer_ << '"';
  }
}

void DisplayExpression::PrettyPrint(PrettyPrinter& printer, SemanticContext* context) {
  if (!header_.empty()) {
    printer << header_;
  }
  if (expression_ != nullptr) {
    fidl_codec::semantic::ExpressionValue value;
    expression_->Execute(context, &value);
    value.UseLinkedHandle(context);
    value.PrettyPrint(printer);
  }
  if (!footer_.empty()) {
    printer << footer_;
  }
}

void MethodDisplay::Dump(std::ostream& os) const {
  for (const auto& input : inputs_) {
    os << "input_field:";
    input->Dump(os);
    os << ";\n";
  }
  for (const auto& result : results_) {
    os << "result:";
    result->Dump(os);
    os << ";\n";
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

void ExpressionValue::UseLinkedHandle(const SemanticContext* context) {
  if (kind_ == kValue) {
    auto handle_value = value_->AsHandleValue();
    if (handle_value != nullptr) {
      set_handle(handle_value->handle().handle);
    }
  }
  if (kind_ == kHandle) {
    if (handle_ != ZX_HANDLE_INVALID) {
      zx_handle_t linked_handle =
          context->handle_semantic()->GetLinkedHandle(context->pid(), handle_);
      if (linked_handle != ZX_HANDLE_INVALID) {
        handle_ = linked_handle;
      }
    }
  }
}

void ExpressionValue::PrettyPrint(PrettyPrinter& printer) {
  switch (kind_) {
    case kUndefined:
      break;
    case kValue:
      value_->PrettyPrint(value_type_, printer);
      break;
    case kHandle: {
      zx_handle_info_t handle_info{.handle = handle_};
      printer.DisplayHandle(handle_info);
      break;
    }
    case kInferredHandleInfo:
      inferred_handle_info_->Display(printer);
      break;
    case kString:
      printer << Red << '"' << string_ << '"' << ResetColor;
      break;
    case kInteger:
      printer << Blue << integer_ << ResetColor;
      break;
  }
}

}  // namespace semantic
}  // namespace fidl_codec

// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/validation_errors.h"

#include <string>

#include "lib/fxl/logging.h"

namespace fidl {
namespace internal {
namespace {

ValidationErrorObserverForTesting* g_validation_error_observer = nullptr;

}  // namespace

// TODO(vardhan):  There are golden files
// (lib/fidl/compiler/interfaces/tests/data/validation/*expected) with these
// strings shared between languages, so changing them here requires changing
// them in all languages, along with the golden files.
const char* ValidationErrorToString(ValidationError error) {
  switch (error) {
    case ValidationError::NONE:
      return "VALIDATION_ERROR_NONE";
    case ValidationError::MISALIGNED_OBJECT:
      return "VALIDATION_ERROR_MISALIGNED_OBJECT";
    case ValidationError::ILLEGAL_MEMORY_RANGE:
      return "VALIDATION_ERROR_ILLEGAL_MEMORY_RANGE";
    case ValidationError::UNEXPECTED_STRUCT_HEADER:
      return "VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER";
    case ValidationError::UNEXPECTED_ARRAY_HEADER:
      return "VALIDATION_ERROR_UNEXPECTED_ARRAY_HEADER";
    case ValidationError::ILLEGAL_HANDLE:
      return "VALIDATION_ERROR_ILLEGAL_HANDLE";
    case ValidationError::UNEXPECTED_INVALID_HANDLE:
      return "VALIDATION_ERROR_UNEXPECTED_INVALID_HANDLE";
    case ValidationError::ILLEGAL_POINTER:
      return "VALIDATION_ERROR_ILLEGAL_POINTER";
    case ValidationError::UNEXPECTED_NULL_POINTER:
      return "VALIDATION_ERROR_UNEXPECTED_NULL_POINTER";
    case ValidationError::MESSAGE_HEADER_INVALID_FLAGS:
      return "VALIDATION_ERROR_MESSAGE_HEADER_INVALID_FLAGS";
    case ValidationError::MESSAGE_HEADER_MISSING_REQUEST_ID:
      return "VALIDATION_ERROR_MESSAGE_HEADER_MISSING_REQUEST_ID";
    case ValidationError::MESSAGE_HEADER_UNKNOWN_METHOD:
      return "VALIDATION_ERROR_MESSAGE_HEADER_UNKNOWN_METHOD";
    case ValidationError::DIFFERENT_SIZED_ARRAYS_IN_MAP:
      return "VALIDATION_ERROR_DIFFERENT_SIZED_ARRAYS_IN_MAP";
    case ValidationError::UNEXPECTED_NULL_UNION:
      return "VALIDATION_ERROR_UNEXPECTED_NULL_UNION";
  }

  return "Unknown error";
}

void ReportValidationError(ValidationError error, std::string* description) {
  if (g_validation_error_observer) {
    g_validation_error_observer->set_last_error(error);
  } else if (description) {
    FXL_LOG(ERROR) << "Invalid message: " << ValidationErrorToString(error)
                   << " (" << *description << ")";
  } else {
    FXL_LOG(ERROR) << "Invalid message: " << ValidationErrorToString(error);
  }
}

ValidationErrorObserverForTesting::ValidationErrorObserverForTesting()
    : last_error_(ValidationError::NONE) {
  FXL_DCHECK(!g_validation_error_observer);
  g_validation_error_observer = this;
}

ValidationErrorObserverForTesting::~ValidationErrorObserverForTesting() {
  FXL_DCHECK(g_validation_error_observer == this);
  g_validation_error_observer = nullptr;
}

ValidationErrorStringStream::ValidationErrorStringStream(std::string* err_msg)
    : err_msg_(err_msg) {}

ValidationErrorStringStream::~ValidationErrorStringStream() {
  if (err_msg_)
    *err_msg_ = stream_.str();
}

}  // namespace internal
}  // namespace fidl

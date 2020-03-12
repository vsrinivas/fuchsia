// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {

struct Position;

struct StartingPoint {
  uint8_t* const addr;
  Position ToPosition() const;
};

struct Position {
  void* addr;
  Position operator+(uint32_t size) const {
    return Position{reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(addr) + size)};
  }
  Position& operator+=(uint32_t size) {
    *this = *this + size;
    return *this;
  }
  template <typename T>
  constexpr T* Get(StartingPoint start) const {
    return reinterpret_cast<T*>(addr);
  }
};

Position StartingPoint::ToPosition() const { return Position{reinterpret_cast<void*>(addr)}; }

class FidlHandleCloser final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, StartingPoint, Position> {
 public:
  FidlHandleCloser(const char** out_error_msg) : out_error_msg_(out_error_msg) {}

  using StartingPoint = StartingPoint;

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = true;

  static constexpr bool kAllowNonNullableCollectionsToBeAbsent = false;

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    // Just follow the pointer into the child object
    *out_position = Position{*object_ptr_ptr};
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle) {
    // Close the handle and mark it as invalid
    zx_handle_close(*handle);
    *handle = ZX_HANDLE_INVALID;
    return Status::kSuccess;
  }

  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    return Status::kSuccess;
  }

  Status EnterEnvelope(Position envelope_position, EnvelopePointer envelope,
                       const fidl_type_t* payload_type) {
    return Status::kSuccess;
  }

  Status LeaveEnvelope(Position envelope_position, EnvelopePointer envelope) {
    return Status::kSuccess;
  }

  void OnError(const char* error) { SetError(error); }

  zx_status_t status() const { return status_; }

 private:
  void SetError(const char* error_msg) {
    if (status_ == ZX_OK) {
      status_ = ZX_ERR_INVALID_ARGS;
      if (out_error_msg_ != nullptr) {
        *out_error_msg_ = error_msg;
      }
    }
  }

  // Message state passed in to the constructor.
  const char** const out_error_msg_;
  zx_status_t status_ = ZX_OK;
};

}  // namespace

zx_status_t fidl_close_handles(const fidl_type_t* type, void* value, const char** out_error_msg) {
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (value == nullptr) {
    set_error("Cannot close handles for null message");
    return ZX_ERR_INVALID_ARGS;
  }
  if (type == nullptr) {
    set_error("Cannot close handles for a null fidl type");
    return ZX_ERR_INVALID_ARGS;
  }

  FidlHandleCloser handle_closer(out_error_msg);
  fidl::Walk(handle_closer, type, StartingPoint{reinterpret_cast<uint8_t*>(value)});

  return handle_closer.status();
}

zx_status_t fidl_close_handles_msg(const fidl_type_t* type, const fidl_msg_t* msg,
                                   const char** out_error_msg) {
  return fidl_close_handles(type, msg->bytes, out_error_msg);
}

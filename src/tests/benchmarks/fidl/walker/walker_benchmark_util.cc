// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "walker_benchmark_util.h"

namespace walker_benchmarks {
namespace internal {

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
  constexpr T* Get() const {
    return reinterpret_cast<T*>(addr);
  }
};

struct EnvelopeCheckpoint {};

class NoOpVisitor final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, Position, EnvelopeCheckpoint> {
 public:
  NoOpVisitor() {}

  using Position = Position;

  static constexpr bool kOnlyWalkResources = false;
  static constexpr bool kContinueAfterConstraintViolation = true;

  Status VisitAbsentPointerInNonNullableCollection(ObjectPointerPointer object_ptr_ptr) {
    OnError("absent pointer disallowed in non-nullable collection");
    return Status::kConstraintViolationError;
  }

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    // Follow the pointer.
    *out_position = Position{*object_ptr_ptr};
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle, zx_rights_t handle_rights,
                     zx_obj_type_t handle_subtype) {
    return Status::kSuccess;
  }

  Status VisitVectorOrStringCount(CountPointer ptr) { return Status::kSuccess; }

  template <typename MaskType>
  Status VisitInternalPadding(Position padding_position, MaskType padding_mask) {
    return Status::kSuccess;
  }

  EnvelopeCheckpoint EnterEnvelope() { return {}; }

  Status LeaveEnvelope(EnvelopePointer envelope, EnvelopeCheckpoint prev_checkpoint) {
    return Status::kSuccess;
  }

  Status VisitUnknownEnvelope(EnvelopePointer envelope, fidl::EnvelopeSource source) {
    return Status::kSuccess;
  }

  void OnError(const char* error) { error_ = error; }

  const char* error() const { return error_; }

 private:
  const char* error_ = nullptr;
};

void Walk(const fidl_type_t* fidl_type, uint8_t* data) {
  NoOpVisitor visitor;
  fidl::Walk(visitor, fidl_type, NoOpVisitor::Position{data});
  ZX_ASSERT(visitor.error() == nullptr);
}

}  // namespace internal
}  // namespace walker_benchmarks

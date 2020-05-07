// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_

#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>

#include <perftest/perftest.h>

#include "linearize_util.h"

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

class NoOpVisitor final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, StartingPoint, Position> {
 public:
  NoOpVisitor() {}

  using StartingPoint = StartingPoint;

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = true;

  static constexpr bool kAllowNonNullableCollectionsToBeAbsent = false;

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

  void OnError(const char* error) { error_ = error; }

  const char* error() const { return error_; }

 private:
  const char* error_ = nullptr;
};

void Walk(const fidl_type_t* fidl_type, uint8_t* data) {
  NoOpVisitor visitor;
  fidl::Walk(visitor, fidl_type, NoOpVisitor::StartingPoint{data});
  ZX_ASSERT(visitor.error() == nullptr);
}

}  // namespace

namespace walker_benchmarks {

template <typename BuilderFunc>
bool WalkerBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = builder();
  uint8_t linearize_buffer[BufferSize<FidlType>];
  auto benchmark_linearize_result = Linearize(nullptr, &aligned_value.value, linearize_buffer);
  auto& linearize_result = benchmark_linearize_result.result;
  ZX_ASSERT(linearize_result.status == ZX_OK && linearize_result.error == nullptr);
  fidl::BytePart bytes = linearize_result.message.Release();

  while (state->KeepRunning()) {
    Walk(FidlType::Type, bytes.data());
  }

  return true;
}

}  // namespace walker_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_

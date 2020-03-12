// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_VISITOR_H_
#define LIB_FIDL_VISITOR_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/internal_callable_traits.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace fidl {

struct NonMutatingVisitorTrait {
  // Types residing in the FIDL message buffer are const
  static constexpr bool kIsConst = true;

  // Message is const
  using ObjectPointerPointer = const void* const* const;
};

struct MutatingVisitorTrait {
  // Types residing in the FIDL message buffer are mutable
  static constexpr bool kIsConst = false;

  // Message is mutable
  using ObjectPointerPointer = void** const;
};

namespace {

// The interface of a FIDL message visitor.
//
// The walker class drives the message traversal, and encoders/decoders/validators etc.
// implement this interface to perform their task.
//
// Visitors should inherit from this class, which has compile-time checks that all visitor interface
// requirements have been met. The walker logic is always parameterized by a concrete implementation
// of this interface, hence there is no virtual method call overhead. MutationTrait is one of
// NonMutatingVisitorTrait or MutatingVisitorTrait.
//
// Many FIDL types do not need special treatment when encoding/decoding. Those that do include:
// - Handles: Transferred to/from handle table.
// - Indirections e.g. nullable fields, strings, vectors: Perform pointer patching.
//
// All pointers passed to the visitor are guaranteed to be alive throughout the duration
// of the message traversal.
// For all callbacks in the visitor, the return value indicates if an error has occurred.
template <typename MutationTrait_, typename StartingPoint_, typename Position_>
class Visitor {
 public:
  using MutationTrait = MutationTrait_;

  template <typename T>
  using Ptr = typename std::conditional<MutationTrait::kIsConst, typename std::add_const<T>::type,
                                        T>::type*;

  // A type encapsulating the starting point of message traversal.
  //
  // Implementations must have the following:
  // - Position ToPosition() const, which returns a |Position| located at the starting point.
  using StartingPoint = StartingPoint_;

  // A type encapsulating the position of the walker within the message. This type is parametric,
  // such that the walker does not assume any memory order between objects. |Position| is tracked
  // by the walker at every level of the coding frame, hence we encourage using a smaller type
  // for |Position|, and placing larger immutable values in |StartingPoint|. For example, in the
  // encoder, |StartingPoint| can be a 64-bit buffer address, while |Position| is a 32-bit offset.
  //
  // Implementations must have the following:
  // - Position operator+(uint32_t size) const, to advance position by |size| in the message.
  // - Position& operator+=(uint32_t size), to advance position by |size| in the message.
  // - template <typename T> Ptr<T> Get(StartingPoint start) const, to cast to a suitable pointer.
  using Position = Position_;

  // ObjectPointerPointer is ([const] void*) *[const]
  using ObjectPointerPointer = typename MutationTrait::ObjectPointerPointer;

  // HandlePointer is ([const] zx_handle_t)*
  using HandlePointer = Ptr<zx_handle_t>;

  // EnvelopePointer is ([const] fidl_envelope_t)*
  using EnvelopePointer = Ptr<fidl_envelope_t>;

  // Status returned by visitor callbacks.
  enum class Status {
    kSuccess = 0,
    kConstraintViolationError,  // recoverable errors
    kMemoryError                // overflow/out-of-bounds etc. Non-recoverable.
  };

  enum class PointeeType { kVectorOrString, kOther };

  // Compile-time interface checking. Code is invisible to the subclass.
 private:
  // Visit an indirection, which can be the data pointer of a string/vector, the data pointer
  // of an envelope from a table, the pointer in a nullable type, etc.
  //
  // If kAllowNonNullableCollectionsToBeAbsent is false, this is only called when
  // the pointer is present.
  // Otherwise, this is called in case of present pointers, as well as non-nullable but absent
  // vectors and strings.
  //
  // |ptr_position|   Position of the pointer.
  // |pointee_type|   Type of the pointee.
  // |object_ptr_ptr| Pointer to the data pointer, obtained from |ptr_position.Get(start)|.
  //                  It can be used to patch the pointer.
  // |inline_size|    Size of the inline part of the target object.
  //                  For vectors, this covers the inline part of all the elements.
  //                  It will not contain any trailing padding between objects.
  // |out_position|   Returns the position where the walker will continue its object traversal.
  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    return Status::kSuccess;
  }

  // Visit a handle. The handle pointer will be mutable if the visitor is mutating.
  // Only called when the handle is present.
  // The handle pointer is derived from |handle_position.Get(start)|.
  Status VisitHandle(Position handle_position, HandlePointer handle_ptr) {
    return Status::kSuccess;
  }

  // Visit a region of padding bytes within message objects. They may be between members of a
  // struct, from after the last member to the end of the struct, or from after a union variant
  // to the end of a union. They should be zero on the wire.
  //
  // N.B. A different type of paddings exist between out-of-line message objects, which are always
  // aligned to |FIDL_ALIGNMENT|. They should be handled accordingly as part of |VisitPointer|.
  //
  // |padding_position| Position of the start of the padding region.
  // |padding_length|   Size of the padding region. It is always positive.
  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    return Status::kSuccess;
  }

  // Called when the walker encounters an envelope.
  // The envelope may be empty or unknown. The implementation should respond accordingly.
  //
  // |payload_type| points to the coding table for the envelope payload. When it is null,
  // either the payload does not require encoding/decoding (e.g. primitives), or the walker
  // has encountered an unknown ordinal.
  //
  // When |EnterEnvelope| returns |Error::kSuccess|, since the data pointer of an envelope is also
  // an indirection, |VisitPointer| will be called on the data pointer. Regardless if the envelope
  // is empty, |LeaveEnvelope| will be called after processing this envelope.
  //
  // Return an error to indicate that the envelope should not be traversed.
  // There will be no corresponding |LeaveEnvelope| call in this case.
  Status EnterEnvelope(Position envelope_position, EnvelopePointer envelope_ptr,
                       const fidl_type_t* payload_type) {
    return Status::kSuccess;
  }

  // Called when the walker finishes visiting all the data in an envelope.
  // Decoder/encoder should validate that the expected number of bytes/handles have been consumed.
  // Linearizer can use this opportunity to set the appropriate num_bytes/num_handles value.
  // It is possible to have nested enter/leave envelope pairs.
  // There will be a matching call to |LeaveEnvelope| for every successful |EnterEnvelope|.
  //
  // |envelope_position| Position of the envelope header.
  // |envelope_ptr|      Pointer to the envelope header that was just processed.
  //                     It is derived from |envelope_position.Get(start)|.
  Status LeaveEnvelope(Position envelope_position, EnvelopePointer envelope_ptr) {
    return Status::kSuccess;
  }

  // Called when a traversal error is encountered on the walker side.
  void OnError(const char* error) {}

  template <typename Visitor_, typename ImplSubType_>
  friend constexpr bool CheckVisitorInterface();
};

template <typename Visitor, typename ImplSubType>
constexpr bool CheckVisitorInterface() {
  static_assert(std::is_base_of<Visitor, ImplSubType>::value,
                "ImplSubType should inherit from fidl::Visitor");

  // kContinueAfterConstraintViolation:
  // - When true, the walker will continue when constraints (e.g. string length) are violated.
  // - When false, the walker will stop upon first error of any kind.
  static_assert(
      std::is_same<decltype(ImplSubType::kContinueAfterConstraintViolation), const bool>::value,
      "ImplSubType must declare constexpr bool kContinueAfterConstraintViolation");

  // kAllowNonNullableCollectionsToBeAbsent:
  // - When true, the walker will allow non-nullable vectors/strings to have a null data pointer
  //   and zero count, treating them as if they are empty (non-null data pointer and zero count).
  // - When false, the above case becomes a constraint violation error.
  static_assert(std::is_same<decltype(ImplSubType::kAllowNonNullableCollectionsToBeAbsent),
                             const bool>::value,
                "ImplSubType must declare constexpr bool kAllowNonNullableCollectionsToBeAbsent");

  static_assert(std::is_same<typename internal::callable_traits<decltype(
                                 &Visitor::StartingPoint::ToPosition)>::return_type,
                             typename Visitor::Position>::value,
                "Incorrect/missing StartingPoint");

  static_assert(internal::SameInterface<decltype(&Visitor::VisitPointer),
                                        decltype(&ImplSubType::VisitPointer)>,
                "Incorrect/missing VisitPointer");
  static_assert(
      internal::SameInterface<decltype(&Visitor::VisitHandle), decltype(&ImplSubType::VisitHandle)>,
      "Incorrect/missing VisitHandle");
  static_assert(internal::SameInterface<decltype(&Visitor::VisitInternalPadding),
                                        decltype(&ImplSubType::VisitInternalPadding)>,
                "Incorrect/missing VisitInternalPadding");
  static_assert(internal::SameInterface<decltype(&Visitor::EnterEnvelope),
                                        decltype(&ImplSubType::EnterEnvelope)>,
                "Incorrect/missing EnterEnvelope");
  static_assert(internal::SameInterface<decltype(&Visitor::LeaveEnvelope),
                                        decltype(&ImplSubType::LeaveEnvelope)>,
                "Incorrect/missing LeaveEnvelope");
  static_assert(
      internal::SameInterface<decltype(&Visitor::OnError), decltype(&ImplSubType::OnError)>,
      "Incorrect/missing OnError");
  return true;
}

}  // namespace

}  // namespace fidl

#endif  // LIB_FIDL_VISITOR_H_

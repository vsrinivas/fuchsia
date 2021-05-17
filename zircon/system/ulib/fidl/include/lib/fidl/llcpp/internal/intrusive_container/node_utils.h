// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_INTRUSIVE_CONTAINER_NODE_UTILS_H_
#define LIB_FIDL_LLCPP_INTERNAL_INTRUSIVE_CONTAINER_NODE_UTILS_H_

#include <lib/fidl/llcpp/internal/intrusive_container/container_utils.h>

namespace fidl {
namespace internal_wavl {
namespace internal {

template <typename DerivedType>
struct CommonNodeStateBase {
  static constexpr bool kNodeCopySupported =
      (DerivedType::kNodeOptions & (NodeOptions::AllowCopy | NodeOptions::AllowCopyFromContainer));
  static constexpr bool kNodeMoveSupported =
      (DerivedType::kNodeOptions & (NodeOptions::AllowMove | NodeOptions::AllowMoveFromContainer));
  static constexpr bool kNodeCopyFromContainerSupported =
      (DerivedType::kNodeOptions & (NodeOptions::AllowCopyFromContainer));
  static constexpr bool kNodeMoveFromContainerSupported =
      (DerivedType::kNodeOptions & (NodeOptions::AllowMoveFromContainer));

  CommonNodeStateBase() = default;
  ~CommonNodeStateBase() = default;

  // Copy-construct a node.
  //
  // It is an error to copy a node that's in a container. Attempting to do so
  // will result in an assertion failure or a default constructed node if
  // assertions are not enabled.
  CommonNodeStateBase(const CommonNodeStateBase& other) : CommonNodeStateBase() {
    static_assert(
        kNodeCopySupported,
        "Node does not allow copy construction.  Consider adding either NodeOptions::AllowCopy or "
        "NodeOptions::AllowCopyFromContainer if appropriate.");
    if constexpr (!kNodeCopyFromContainerSupported) {
      ZX_DEBUG_ASSERT(!(static_cast<const DerivedType&>(other)).InContainer());
    }
  }

  // Copy-assign a node.
  //
  // It is an error to copy-assign to or from a node that's in a container.
  // Attempting to do so will result in an assertion failure or a no-op if
  // assertions are not enabled.
  //
  // |this| will not be modified.
  CommonNodeStateBase& operator=(const CommonNodeStateBase& other) {
    static_assert(
        kNodeCopySupported,
        "Node does not allow copy assignment.  Consider adding either NodeOptions::AllowCopy or "
        "NodeOptions::AllowCopyFromContainer if appropriate.");
    if constexpr (!kNodeCopyFromContainerSupported) {
      ZX_DEBUG_ASSERT(!(static_cast<const DerivedType&>(*this)).InContainer());
      ZX_DEBUG_ASSERT(!(static_cast<const DerivedType&>(other)).InContainer());
    }
    // To avoid corrupting the container, |this| must remain unmodified.
    return *this;
  }

  // Move-construction and move-assignment, when permitted, have the same
  // behavior as copy-construction and copy-assignment, respectively. |other|
  // and |this| are never modified.
  CommonNodeStateBase(CommonNodeStateBase&& other) : CommonNodeStateBase() {
    static_assert(
        kNodeMoveSupported,
        "Node does not allow move construction.  Consider adding either NodeOptions::AllowMove or "
        "NodeOptions::AllowMoveFromContainer if appropriate.");
    if constexpr (!kNodeMoveFromContainerSupported) {
      ZX_DEBUG_ASSERT(!(static_cast<const DerivedType&>(other)).InContainer());
    }
  }

  CommonNodeStateBase& operator=(CommonNodeStateBase&& other) {
    static_assert(
        kNodeMoveSupported,
        "Node does not allow move assignment.  Consider adding either NodeOptions::AllowMove or "
        "NodeOptions::AllowMoveFromContainer if appropriate.");
    if constexpr (!kNodeMoveFromContainerSupported) {
      ZX_DEBUG_ASSERT(!(static_cast<const DerivedType&>(*this)).InContainer());
      ZX_DEBUG_ASSERT(!(static_cast<const DerivedType&>(other)).InContainer());
    }
    // To avoid corrupting the container, |this| must remain unmodified.
    return *this;
  }
};

}  // namespace internal
}  // namespace internal_wavl
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_INTRUSIVE_CONTAINER_NODE_UTILS_H_

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_COMPONENT_CPP_TREE_HANDLER_SETTINGS_H_
#define LIB_INSPECT_COMPONENT_CPP_TREE_HANDLER_SETTINGS_H_

#include <lib/stdcompat/optional.h>

namespace inspect {

// TreeServerSendPreference describes how the Inspect VMO should be served.
//
// The server has a primary behavior and a failure behavior. These describe the way that the
// Inspector's VMO handle will be duplicated. The options in general are:
// - Frozen: this is copy-on-write.
// - Live: updates to the server side VMO propagate to the client. The client is read-only.
// - DeepCopy: completely copies the VMO data into a new VMO.
//
// The primary behavior is always configurable. By default, the primary behavior is Frozen.
//
// The failure behavior is configurable when the primary behavior is Frozen. In that case,
// the failure behavior can be set to either Live or DeepCopy. The default is Live.
//
// The ultimate fallback behavior is always to send a Live VMO.
class TreeServerSendPreference {
 public:
  // Default behavior is to send a Frozen VMO; on failure, it will send a Live VMO.
  TreeServerSendPreference() = default;

  // Defines the behavior of the VMO sent over this service.
  enum class Type {

    // The VMO is copy-on-write.
    Frozen,

    // The VMO is a live reference to the original. Updates to the server side
    // affect the client side.
    Live,

    // The VMO is fully copied before being sent.
    DeepCopy,
  };

  // Freeze the VMO if possible. On failure, do `failure`.
  // `failure` should not be Type::Frozen.
  static constexpr TreeServerSendPreference Frozen(Type failure) {
    return TreeServerSendPreference(Type::Frozen, failure);
  }

  // Send a live VMO.
  static constexpr TreeServerSendPreference Live() {
    return TreeServerSendPreference(Type::Live, cpp17::nullopt);
  }

  // Send a true copy of the VMO.
  static constexpr TreeServerSendPreference DeepCopy() {
    return TreeServerSendPreference(Type::DeepCopy, cpp17::nullopt);
  }

  constexpr Type PrimaryBehavior() { return primary_send_type_; }
  constexpr cpp17::optional<Type> FailureBehavior() { return failure_behavior_; }

 private:
  explicit constexpr TreeServerSendPreference(Type primary, cpp17::optional<Type> failure)
      : primary_send_type_(primary), failure_behavior_(failure) {}

  const Type primary_send_type_ = Type::Frozen;
  const cpp17::optional<Type> failure_behavior_ = Type::Live;
};

struct TreeHandlerSettings {
  // Control the way a VMO is served for snapshotting.
  TreeServerSendPreference snapshot_behavior;
};

}  // namespace inspect

#endif  // LIB_INSPECT_COMPONENT_CPP_TREE_HANDLER_SETTINGS_H_

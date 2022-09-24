// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_SERVICE_CPP_TREE_HANDLER_SETTINGS_H_
#define LIB_INSPECT_SERVICE_CPP_TREE_HANDLER_SETTINGS_H_

#include <lib/stdcompat/optional.h>

namespace inspect {

// TreeServerSendPreference describes how the VMO should be served.
class TreeServerSendPreference {
 public:
  // Default behavior is to send a Frozen VMO; on failure, it will send a Live VMO.
  TreeServerSendPreference() = default;

  enum class Type {
    Frozen,
    Live,
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

#endif  // LIB_INSPECT_SERVICE_CPP_TREE_HANDLER_SETTINGS_H_

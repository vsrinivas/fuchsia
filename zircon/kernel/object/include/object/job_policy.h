// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_JOB_POLICY_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_JOB_POLICY_H_

#include <stdint.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <kernel/timer.h>

typedef uint64_t pol_cookie_t;

// JobPolicy is a value type that provides a space-efficient encoding of the policies defined in the
// policy.h public header.
//
// JobPolicy encodes two kinds of policy, basic and timer slack.
//
// Basic policy is logically an array of zx_policy_basic elements. For example:
//
//   zx_policy_basic policy[] = {
//      { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL },
//      { ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_ALLOW },
//      { ZX_POL_NEW_FIFO, ZX_POL_ACTION_ALLOW_EXCEPTION },
//      { ZX_POL_VMAR_WX, ZX_POL_ACTION_KILL }}
//
// Timer slack policy defines the type and minimum amount of slack that will be applied to timer
// and deadline events.
class JobPolicy {
 public:
  JobPolicy() = delete;
  JobPolicy(const JobPolicy& parent);
  static JobPolicy CreateRootPolicy();

  // Merge array |policy| of length |count| into this object.
  //
  // |mode| controls what happens when the policies in |policy| and this object intersect. |mode|
  // must be one of:
  //
  // ZX_JOB_POL_RELATIVE - Conflicting policies are ignored and will not cause the call to fail.
  //
  // ZX_JOB_POL_ABSOLUTE - If any of the policies in |policy| conflict with those in this object,
  //   the call will fail with an error and this object will not be modified.
  //
  zx_status_t AddBasicPolicy(uint32_t mode, const zx_policy_basic_v2_t* policy, size_t count);

  // Returns the action (e.g. ZX_POL_ACTION_ALLOW) for the specified |condition|.
  //
  // This method asserts if |policy| is invalid, and returns ZX_POL_ACTION_DENY for all other
  // failure modes.
  uint32_t QueryBasicPolicy(uint32_t condition) const;

  // Returns if the action for the specified condition can be overriden, so it returns
  // ZX_POL_OVERRIDE_ALLOW or ZX_POL_OVERRIDE_DENY.
  uint32_t QueryBasicPolicyOverride(uint32_t condition) const;

  // Sets the timer slack policy.
  //
  // |slack.amount| must be >= 0.
  void SetTimerSlack(TimerSlack slack);

  // Returns the timer slack policy.
  TimerSlack GetTimerSlack() const;

  bool operator==(const JobPolicy& rhs) const;
  bool operator!=(const JobPolicy& rhs) const;

  // Increment the kcounter for the given |action| and |condition|.
  //
  // action must be < ZX_POL_ACTION_MAX and condition must be < ZX_POL_MAX.
  //
  // For example: IncrementCounter(ZX_POL_ACTION_KILL, ZX_POL_NEW_CHANNEL);
  static void IncrementCounter(uint32_t action, uint32_t condition);

 private:
  JobPolicy(pol_cookie_t cookie, const TimerSlack& slack);
  // Remember, JobPolicy is a value type so think carefully before increasing its size.
  //
  // Const instances of JobPolicy must be immutable to ensure thread-safety.
  pol_cookie_t cookie_{};
  TimerSlack slack_{TimerSlack::none()};
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_JOB_POLICY_H_

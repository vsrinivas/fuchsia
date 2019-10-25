// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "object/job_policy.h"

#include <assert.h>
#include <err.h>
#include <lib/counters.h>
#include <zircon/syscalls/policy.h>

#include <fbl/algorithm.h>
#include <fbl/bitfield.h>
#include <kernel/deadline.h>

namespace {
// It is critical that this array contain all "new object" policies because it's used to implement
// ZX_NEW_ANY.
constexpr uint32_t kNewObjectPolicies[]{
    ZX_POL_NEW_VMO,     ZX_POL_NEW_CHANNEL, ZX_POL_NEW_EVENT, ZX_POL_NEW_EVENTPAIR,
    ZX_POL_NEW_PORT,    ZX_POL_NEW_SOCKET,  ZX_POL_NEW_FIFO,  ZX_POL_NEW_TIMER,
    ZX_POL_NEW_PROCESS, ZX_POL_NEW_PROFILE,
};
static_assert(
    fbl::count_of(kNewObjectPolicies) + 5 == ZX_POL_MAX,
    "please update JobPolicy::AddPartial, JobPolicy::QueryBasicPolicy, kNewObjectPolicies,"
    "and the add_basic_policy_deny_any_new() test");

FBL_BITFIELD_DEF_START(JobPolicyBits, uint64_t)
FBL_BITFIELD_MEMBER(bad_handle, 0, 3);
FBL_BITFIELD_MEMBER(bad_handle_override, 3, 1);
FBL_BITFIELD_MEMBER(wrong_object, 4, 3);
FBL_BITFIELD_MEMBER(wrong_object_override, 7, 1);
FBL_BITFIELD_MEMBER(vmar_wx, 8, 3);
FBL_BITFIELD_MEMBER(vmar_wx_override, 11, 1);
FBL_BITFIELD_MEMBER(new_vmo, 12, 3);
FBL_BITFIELD_MEMBER(new_vmo_override, 15, 1);
FBL_BITFIELD_MEMBER(new_channel, 16, 3);
FBL_BITFIELD_MEMBER(new_channel_override, 19, 1);
FBL_BITFIELD_MEMBER(new_event, 20, 3);
FBL_BITFIELD_MEMBER(new_event_override, 23, 1);
FBL_BITFIELD_MEMBER(new_eventpair, 24, 3);
FBL_BITFIELD_MEMBER(new_eventpair_override, 27, 1);
FBL_BITFIELD_MEMBER(new_port, 28, 3);
FBL_BITFIELD_MEMBER(new_port_override, 31, 1);
FBL_BITFIELD_MEMBER(new_socket, 32, 3);
FBL_BITFIELD_MEMBER(new_socket_override, 35, 1);
FBL_BITFIELD_MEMBER(new_fifo, 36, 3);
FBL_BITFIELD_MEMBER(new_fifo_override, 39, 1);
FBL_BITFIELD_MEMBER(new_timer, 40, 3);
FBL_BITFIELD_MEMBER(new_timer_override, 43, 1);
FBL_BITFIELD_MEMBER(new_process, 44, 3);
FBL_BITFIELD_MEMBER(new_process_override, 47, 1);
FBL_BITFIELD_MEMBER(new_profile, 48, 3);
FBL_BITFIELD_MEMBER(new_profile_override, 51, 1);
FBL_BITFIELD_MEMBER(ambient_mark_vmo_exec, 52, 3);
FBL_BITFIELD_MEMBER(ambient_mark_vmo_exec_override, 55, 1);
FBL_BITFIELD_MEMBER(unused_bits, 56, 8);
FBL_BITFIELD_DEF_END();

#define SET_POL_ENTRY(policy_member, policy, override)                         \
  do {                                                                         \
    if (bits->policy_member##_override == ZX_POL_OVERRIDE_ALLOW) {             \
      bits->policy_member = policy;                                            \
      bits->policy_member##_override = override;                               \
      return ZX_OK;                                                            \
    }                                                                          \
    if ((bits->policy_member == policy) && (override == ZX_POL_OVERRIDE_DENY)) \
      return ZX_OK;                                                            \
    return (mode == ZX_JOB_POL_ABSOLUTE) ? ZX_ERR_ALREADY_EXISTS : ZX_OK;      \
  } while (0)

zx_status_t AddPartial(uint32_t mode, uint32_t condition, uint32_t policy, uint32_t override,
                       JobPolicyBits* bits) {
  if (policy >= ZX_POL_ACTION_MAX)
    return ZX_ERR_NOT_SUPPORTED;

  if (override > ZX_POL_OVERRIDE_DENY)
    return ZX_ERR_INVALID_ARGS;

  switch (condition) {
    case ZX_POL_BAD_HANDLE:
      SET_POL_ENTRY(bad_handle, policy, override);
    case ZX_POL_WRONG_OBJECT:
      SET_POL_ENTRY(wrong_object, policy, override);
    case ZX_POL_VMAR_WX:
      SET_POL_ENTRY(vmar_wx, policy, override);
    case ZX_POL_NEW_VMO:
      SET_POL_ENTRY(new_vmo, policy, override);
    case ZX_POL_NEW_CHANNEL:
      SET_POL_ENTRY(new_channel, policy, override);
    case ZX_POL_NEW_EVENT:
      SET_POL_ENTRY(new_event, policy, override);
    case ZX_POL_NEW_EVENTPAIR:
      SET_POL_ENTRY(new_eventpair, policy, override);
    case ZX_POL_NEW_PORT:
      SET_POL_ENTRY(new_port, policy, override);
    case ZX_POL_NEW_SOCKET:
      SET_POL_ENTRY(new_socket, policy, override);
    case ZX_POL_NEW_FIFO:
      SET_POL_ENTRY(new_fifo, policy, override);
    case ZX_POL_NEW_TIMER:
      SET_POL_ENTRY(new_timer, policy, override);
    case ZX_POL_NEW_PROCESS:
      SET_POL_ENTRY(new_process, policy, override);
    case ZX_POL_NEW_PROFILE:
      SET_POL_ENTRY(new_profile, policy, override);
    case ZX_POL_AMBIENT_MARK_VMO_EXEC:
      SET_POL_ENTRY(ambient_mark_vmo_exec, policy, override);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

#undef SET_POL_ENTRY

zx_status_t SetOverride(JobPolicyBits* policy, uint32_t condition, uint32_t override) {
  switch (condition) {
    case ZX_POL_BAD_HANDLE:
      policy->bad_handle_override = override;
      break;
    case ZX_POL_WRONG_OBJECT:
      policy->wrong_object_override = override;
      break;
    case ZX_POL_VMAR_WX:
      policy->vmar_wx_override = override;
      break;
    case ZX_POL_NEW_VMO:
      policy->new_vmo_override = override;
      break;
    case ZX_POL_NEW_CHANNEL:
      policy->new_channel_override = override;
      break;
    case ZX_POL_NEW_EVENT:
      policy->new_event_override = override;
      break;
    case ZX_POL_NEW_EVENTPAIR:
      policy->new_eventpair_override = override;
      break;
    case ZX_POL_NEW_PORT:
      policy->new_port_override = override;
      break;
    case ZX_POL_NEW_SOCKET:
      policy->new_socket_override = override;
      break;
    case ZX_POL_NEW_FIFO:
      policy->new_fifo_override = override;
      break;
    case ZX_POL_NEW_TIMER:
      policy->new_timer_override = override;
      break;
    case ZX_POL_NEW_PROCESS:
      policy->new_process_override = override;
      break;
    case ZX_POL_NEW_PROFILE:
      policy->new_profile_override = override;
      break;
    case ZX_POL_AMBIENT_MARK_VMO_EXEC:
      policy->ambient_mark_vmo_exec_override = override;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

uint64_t GetAction(JobPolicyBits policy, uint32_t condition) {
  switch (condition) {
    case ZX_POL_BAD_HANDLE:
      return policy.bad_handle;
    case ZX_POL_WRONG_OBJECT:
      return policy.wrong_object;
    case ZX_POL_VMAR_WX:
      return policy.vmar_wx;
    case ZX_POL_NEW_VMO:
      return policy.new_vmo;
    case ZX_POL_NEW_CHANNEL:
      return policy.new_channel;
    case ZX_POL_NEW_EVENT:
      return policy.new_event;
    case ZX_POL_NEW_EVENTPAIR:
      return policy.new_eventpair;
    case ZX_POL_NEW_PORT:
      return policy.new_port;
    case ZX_POL_NEW_SOCKET:
      return policy.new_socket;
    case ZX_POL_NEW_FIFO:
      return policy.new_fifo;
    case ZX_POL_NEW_TIMER:
      return policy.new_timer;
    case ZX_POL_NEW_PROCESS:
      return policy.new_process;
    case ZX_POL_NEW_PROFILE:
      return policy.new_profile;
    case ZX_POL_AMBIENT_MARK_VMO_EXEC:
      return policy.ambient_mark_vmo_exec;
    default:
      return ZX_POL_ACTION_DENY;
  }
}

uint64_t GetOverride(JobPolicyBits policy, uint32_t condition) {
  switch (condition) {
    case ZX_POL_BAD_HANDLE:
      return policy.bad_handle_override;
    case ZX_POL_WRONG_OBJECT:
      return policy.wrong_object_override;
    case ZX_POL_VMAR_WX:
      return policy.vmar_wx_override;
    case ZX_POL_NEW_VMO:
      return policy.new_vmo_override;
    case ZX_POL_NEW_CHANNEL:
      return policy.new_channel_override;
    case ZX_POL_NEW_EVENT:
      return policy.new_event_override;
    case ZX_POL_NEW_EVENTPAIR:
      return policy.new_eventpair_override;
    case ZX_POL_NEW_PORT:
      return policy.new_port_override;
    case ZX_POL_NEW_SOCKET:
      return policy.new_socket_override;
    case ZX_POL_NEW_FIFO:
      return policy.new_fifo_override;
    case ZX_POL_NEW_TIMER:
      return policy.new_timer_override;
    case ZX_POL_NEW_PROCESS:
      return policy.new_process_override;
    case ZX_POL_NEW_PROFILE:
      return policy.new_profile_override;
    case ZX_POL_AMBIENT_MARK_VMO_EXEC:
      return policy.ambient_mark_vmo_exec_override;
    default:
      return ZX_POL_ACTION_DENY;
  }
}

// The policy applied to the root job allows everything and can override anything.
constexpr uint64_t GetRootJobBitsPolicy() {
  static_assert((ZX_POL_ACTION_ALLOW == 0u) && (ZX_POL_OVERRIDE_ALLOW == 0u));
  constexpr JobPolicyBits policy(0u);
  return policy;
}

}  // namespace

JobPolicy::JobPolicy(const JobPolicy& parent) : cookie_(parent.cookie_), slack_(parent.slack_) {}
JobPolicy::JobPolicy(pol_cookie_t cookie, const TimerSlack& slack)
    : cookie_(cookie), slack_(slack) {}

// static
JobPolicy JobPolicy::CreateRootPolicy() {
  return JobPolicy(GetRootJobBitsPolicy(), TimerSlack::none());
}

zx_status_t JobPolicy::AddBasicPolicy(uint32_t mode, const zx_policy_basic_t* policy_input,
                                      size_t policy_count) {
  // Don't allow overlong policies.
  if (policy_count > ZX_POL_MAX) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t res = ZX_OK;
  JobPolicyBits pol_new(cookie_);
  bool has_new_any = false;

  for (size_t ix = 0; ix != policy_count; ++ix) {
    const auto& in = policy_input[ix];

    if (in.condition == ZX_POL_NEW_ANY) {
      for (auto cond : kNewObjectPolicies) {
        res = AddPartial(mode, cond, in.policy, ZX_POL_OVERRIDE_ALLOW, &pol_new);
        if (res != ZX_OK)
          return res;
      }
      has_new_any = true;
    } else {
      res = AddPartial(mode, in.condition, in.policy, ZX_POL_OVERRIDE_DENY, &pol_new);
      if (res != ZX_OK)
        return res;
    }
  }

  if (has_new_any) {
    for (auto cond : kNewObjectPolicies) {
      auto res = SetOverride(&pol_new, cond, ZX_POL_OVERRIDE_DENY);
      if (res != ZX_OK)
        return res;
    }
  }

  cookie_ = pol_new.value;
  return ZX_OK;
}

uint32_t JobPolicy::QueryBasicPolicy(uint32_t condition) const {
  JobPolicyBits policy(cookie_);
  return static_cast<uint32_t>(GetAction(policy, condition));
}

uint32_t JobPolicy::QueryBasicPolicyOverride(uint32_t condition) const {
  JobPolicyBits policy(cookie_);
  return static_cast<uint32_t>(GetOverride(policy, condition));
}

void JobPolicy::SetTimerSlack(TimerSlack slack) { slack_ = slack; }

TimerSlack JobPolicy::GetTimerSlack() const { return slack_; }

bool JobPolicy::operator==(const JobPolicy& rhs) const {
  if (this == &rhs) {
    return true;
  }

  return cookie_ == rhs.cookie_ && slack_ == rhs.slack_;
}

bool JobPolicy::operator!=(const JobPolicy& rhs) const { return !operator==(rhs); }

// Evaluates to the name of the kcounter for the given action and condition.
//
// E.g. COUNTER(deny, new_channel) -> policy_action_deny_new_channel_count
#define COUNTER(action, condition) policy_##action##_##condition##_count

// Defines a kcounter for the given action and condition.
#define DEFINE_COUNTER(action, condition) \
  KCOUNTER(COUNTER(action, condition), "policy." #action "." #condition)

// Evaluates to the name of an array of pointers to Counter objects.
//
// See DEFINE_COUNTER_ARRAY for details.
#define COUNTER_ARRAY(action) counters_##action

// Defines kcounters for the given action and creates an array named |COUNTER_ARRAY(action)|.
//
// The array has length ZX_POL_MAX and contains pointers to the Counter objects. The array should be
// indexed by condition. Note, some array elements may be null.
//
// Example:
//
//     DEFINE_COUNTER_ARRAY(deny);
//     kcounter_add(*COUNTER_ARRAY(deny)[ZX_POL_NEW_CHANNEL], 1);
//
#define DEFINE_COUNTER_ARRAY(action)                                            \
  DEFINE_COUNTER(action, bad_handle)                                            \
  DEFINE_COUNTER(action, wrong_object)                                          \
  DEFINE_COUNTER(action, vmar_wx)                                               \
  DEFINE_COUNTER(action, new_vmo)                                               \
  DEFINE_COUNTER(action, new_channel)                                           \
  DEFINE_COUNTER(action, new_event)                                             \
  DEFINE_COUNTER(action, new_eventpair)                                         \
  DEFINE_COUNTER(action, new_port)                                              \
  DEFINE_COUNTER(action, new_socket)                                            \
  DEFINE_COUNTER(action, new_fifo)                                              \
  DEFINE_COUNTER(action, new_timer)                                             \
  DEFINE_COUNTER(action, new_process)                                           \
  DEFINE_COUNTER(action, new_profile)                                           \
  DEFINE_COUNTER(action, ambient_mark_vmo_exec)                                 \
  static constexpr const Counter* const COUNTER_ARRAY(action)[] = {             \
      [ZX_POL_BAD_HANDLE] = &COUNTER(action, bad_handle),                       \
      [ZX_POL_WRONG_OBJECT] = &COUNTER(action, wrong_object),                   \
      [ZX_POL_VMAR_WX] = &COUNTER(action, vmar_wx),                             \
      [ZX_POL_NEW_ANY] = nullptr, /* ZX_POL_NEW_ANY is a pseudo condition */    \
      [ZX_POL_NEW_VMO] = &COUNTER(action, new_vmo),                             \
      [ZX_POL_NEW_CHANNEL] = &COUNTER(action, new_channel),                     \
      [ZX_POL_NEW_EVENT] = &COUNTER(action, new_event),                         \
      [ZX_POL_NEW_EVENTPAIR] = &COUNTER(action, new_eventpair),                 \
      [ZX_POL_NEW_PORT] = &COUNTER(action, new_port),                           \
      [ZX_POL_NEW_SOCKET] = &COUNTER(action, new_socket),                       \
      [ZX_POL_NEW_FIFO] = &COUNTER(action, new_fifo),                           \
      [ZX_POL_NEW_TIMER] = &COUNTER(action, new_timer),                         \
      [ZX_POL_NEW_PROCESS] = &COUNTER(action, new_process),                     \
      [ZX_POL_NEW_PROFILE] = &COUNTER(action, new_profile),                     \
      [ZX_POL_AMBIENT_MARK_VMO_EXEC] = &COUNTER(action, ambient_mark_vmo_exec), \
  };                                                                            \
  static_assert(fbl::count_of(COUNTER_ARRAY(action)) == ZX_POL_MAX);

// Counts policy violations resulting in ZX_POL_ACTION_DENY or ZX_POL_ACTION_DENY_EXCEPTION.
DEFINE_COUNTER_ARRAY(deny)
// Counts policy violations resulting in ZX_POL_ACTION_KILL.
DEFINE_COUNTER_ARRAY(kill)
static_assert(ZX_POL_ACTION_MAX == 5, "add another instantiation of DEFINE_COUNTER_ARRAY");

void JobPolicy::IncrementCounter(uint32_t action, uint32_t condition) {
  DEBUG_ASSERT(action < ZX_POL_ACTION_MAX);
  DEBUG_ASSERT(condition < ZX_POL_MAX);

  const Counter* counter = nullptr;
  switch (action) {
    case ZX_POL_ACTION_DENY:
    case ZX_POL_ACTION_DENY_EXCEPTION:
      counter = COUNTER_ARRAY(deny)[condition];
      break;
    case ZX_POL_ACTION_KILL:
      counter = COUNTER_ARRAY(kill)[condition];
      break;
  };
  if (!counter) {
    return;
  }
  kcounter_add(*counter, 1);
}

#undef COUNTER
#undef DEFINE_COUNTER
#undef COUNTER_ARRAY
#undef DEFINE_COUNTER_ARRAY

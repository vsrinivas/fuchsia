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

template <uint32_t condition>
struct FieldSelector {};

#define FIELD_SELECTOR_DEF(id, name)                                           \
  template <>                                                                  \
  struct FieldSelector<id> {                                                   \
    static auto& Action(JobPolicyBits* jpb) { return jpb->name; }              \
    static auto& Override(JobPolicyBits* jpb) { return jpb->name##_override; } \
  }

FIELD_SELECTOR_DEF(ZX_POL_BAD_HANDLE, bad_handle);
FIELD_SELECTOR_DEF(ZX_POL_WRONG_OBJECT, wrong_object);
FIELD_SELECTOR_DEF(ZX_POL_VMAR_WX, vmar_wx);
FIELD_SELECTOR_DEF(ZX_POL_NEW_VMO, new_vmo);
FIELD_SELECTOR_DEF(ZX_POL_NEW_CHANNEL, new_channel);
FIELD_SELECTOR_DEF(ZX_POL_NEW_EVENT, new_event);
FIELD_SELECTOR_DEF(ZX_POL_NEW_EVENTPAIR, new_eventpair);
FIELD_SELECTOR_DEF(ZX_POL_NEW_PORT, new_port);
FIELD_SELECTOR_DEF(ZX_POL_NEW_SOCKET, new_socket);
FIELD_SELECTOR_DEF(ZX_POL_NEW_FIFO, new_fifo);
FIELD_SELECTOR_DEF(ZX_POL_NEW_TIMER, new_timer);
FIELD_SELECTOR_DEF(ZX_POL_NEW_PROCESS, new_profile);
FIELD_SELECTOR_DEF(ZX_POL_NEW_PROFILE, new_profile);
FIELD_SELECTOR_DEF(ZX_POL_AMBIENT_MARK_VMO_EXEC, ambient_mark_vmo_exec);

#define CASE_RETURN_ACTION(id, bits) \
  case id:                           \
    return FieldSelector<id>::Action(bits)

#define CASE_RETURN_OVERRIDE(id, bits) \
  case id:                             \
    return FieldSelector<id>::Override(bits)

#define CASE_SET_OVERRIDE(id, bits, override)     \
  case id:                                        \
    FieldSelector<id>::Override(bits) = override; \
    break

#define CASE_SET_ENTRY(id, bits, action, override)                                         \
  case id: {                                                                               \
    if (FieldSelector<id>::Override(bits) == ZX_POL_OVERRIDE_ALLOW) {                      \
      FieldSelector<id>::Action(bits) = action;                                            \
      FieldSelector<id>::Override(bits) = override;                                        \
      return ZX_OK;                                                                        \
    }                                                                                      \
    if ((FieldSelector<id>::Action(bits) == action) && (override == ZX_POL_OVERRIDE_DENY)) \
      return ZX_OK;                                                                        \
    break;                                                                                 \
  }

zx_status_t AddPartial(uint32_t mode, uint32_t condition, uint32_t action, uint32_t override,
                       JobPolicyBits* bits) {
  if (action >= ZX_POL_ACTION_MAX)
    return ZX_ERR_NOT_SUPPORTED;

  if (override > ZX_POL_OVERRIDE_DENY)
    return ZX_ERR_INVALID_ARGS;

  switch (condition) {
    CASE_SET_ENTRY(ZX_POL_BAD_HANDLE, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_WRONG_OBJECT, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_VMAR_WX, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_VMO, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_CHANNEL, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_EVENT, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_EVENTPAIR, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_PORT, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_SOCKET, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_FIFO, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_TIMER, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_PROCESS, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_NEW_PROFILE, bits, action, override);
    CASE_SET_ENTRY(ZX_POL_AMBIENT_MARK_VMO_EXEC, bits, action, override);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  // If we are here setting policy failed, which migth not matter
  // if it is a relative (best effort) mode.
  return (mode == ZX_JOB_POL_ABSOLUTE) ? ZX_ERR_ALREADY_EXISTS : ZX_OK;
}

zx_status_t SetOverride(JobPolicyBits* policy, uint32_t condition, uint32_t override) {
  switch (condition) {
    CASE_SET_OVERRIDE(ZX_POL_BAD_HANDLE, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_WRONG_OBJECT, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_VMAR_WX, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_VMO, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_CHANNEL, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_EVENT, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_EVENTPAIR, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_PORT, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_SOCKET, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_FIFO, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_TIMER, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_PROCESS, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_NEW_PROFILE, policy, override);
    CASE_SET_OVERRIDE(ZX_POL_AMBIENT_MARK_VMO_EXEC, policy, override);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

uint64_t GetAction(JobPolicyBits policy, uint32_t condition) {
  switch (condition) {
    CASE_RETURN_ACTION(ZX_POL_BAD_HANDLE, &policy);
    CASE_RETURN_ACTION(ZX_POL_WRONG_OBJECT, &policy);
    CASE_RETURN_ACTION(ZX_POL_VMAR_WX, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_VMO, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_CHANNEL, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_EVENT, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_EVENTPAIR, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_PORT, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_SOCKET, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_FIFO, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_TIMER, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_PROCESS, &policy);
    CASE_RETURN_ACTION(ZX_POL_NEW_PROFILE, &policy);
    CASE_RETURN_ACTION(ZX_POL_AMBIENT_MARK_VMO_EXEC, &policy);
    default:
      return ZX_POL_ACTION_DENY;
  }
}

uint64_t GetOverride(JobPolicyBits policy, uint32_t condition) {
  switch (condition) {
    CASE_RETURN_OVERRIDE(ZX_POL_BAD_HANDLE, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_WRONG_OBJECT, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_VMAR_WX, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_VMO, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_CHANNEL, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_EVENT, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_EVENTPAIR, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_PORT, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_SOCKET, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_FIFO, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_TIMER, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_PROCESS, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_NEW_PROFILE, &policy);
    CASE_RETURN_OVERRIDE(ZX_POL_AMBIENT_MARK_VMO_EXEC, &policy);
    default:
      return ZX_POL_OVERRIDE_DENY;
  }
}

#undef CASE_RETURN_ACTION
#undef CASE_RETURN_OVERRIDE
#undef CASE_SET_OVERRIDE
#undef CASE_SET_ENTRY

}  // namespace

JobPolicy::JobPolicy(const JobPolicy& parent) : cookie_(parent.cookie_), slack_(parent.slack_) {}
JobPolicy::JobPolicy(pol_cookie_t cookie, const TimerSlack& slack)
    : cookie_(cookie), slack_(slack) {}

// static
JobPolicy JobPolicy::CreateRootPolicy() {
  static_assert((ZX_POL_ACTION_ALLOW == 0u) && (ZX_POL_OVERRIDE_ALLOW == 0u));
  constexpr JobPolicyBits policy(0u);
  return JobPolicy(policy.value, TimerSlack::none());
}

zx_status_t JobPolicy::AddBasicPolicy(uint32_t mode, const zx_policy_basic_v2_t* policy_input,
                                      size_t policy_count) {
  // Don't allow overlong policies.
  if (policy_count > ZX_POL_MAX) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t res = ZX_OK;
  JobPolicyBits pol_new(cookie_);
  bool has_new_any = false;
  uint32_t new_any_override = 0;

  for (size_t ix = 0; ix != policy_count; ++ix) {
    const auto& in = policy_input[ix];

    if (in.condition == ZX_POL_NEW_ANY) {
      for (auto cond : kNewObjectPolicies) {
        res = AddPartial(mode, cond, in.action, ZX_POL_OVERRIDE_ALLOW, &pol_new);
        if (res != ZX_OK)
          return res;
      }
      has_new_any = true;
      new_any_override = in.flags;
    } else {
      res = AddPartial(mode, in.condition, in.action, in.flags, &pol_new);
      if (res != ZX_OK)
        return res;
    }
  }

  if (has_new_any) {
    for (auto cond : kNewObjectPolicies) {
      auto res = SetOverride(&pol_new, cond, new_any_override);
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

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <object/job_policy.h>

#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <kernel/deadline.h>
#include <zircon/syscalls/policy.h>

namespace {

constexpr uint32_t kDefaultAction = ZX_POL_ACTION_ALLOW;

constexpr pol_cookie_t kPolicyEmpty = 0u;

// The encoding of the basic policy is done 4 bits per each item.
//
// - When the top bit is 1, the lower 3 bits contain the action
//   (ZX_POL_ACTION_ALLOW, ZX_POL_ACTION_DENY, etc.)
//
// - When the top bit is 0 then its the default policy and other bits
//   should be zero so that kPolicyEmpty == 0 meets the requirement of
//   all entries being default.

union Encoding {
    static constexpr uint8_t kExplicitBit = 0b1000;
    static constexpr uint8_t kActionBits  = 0b0111;

    // Indicates the policies are fully encoded in the cookie.
    static constexpr uint8_t kPolicyInCookie = 0;

    pol_cookie_t encoded;
    struct {
        uint64_t bad_handle      :  4;
        uint64_t wrong_obj       :  4;
        uint64_t vmar_wx         :  4;
        uint64_t new_vmo         :  4;
        uint64_t new_channel     :  4;
        uint64_t new_event       :  4;
        uint64_t new_eventpair   :  4;
        uint64_t new_port        :  4;
        uint64_t new_socket      :  4;
        uint64_t new_fifo        :  4;
        uint64_t new_timer       :  4;
        uint64_t new_process     :  4;
        uint64_t new_profile     :  4;
        uint64_t unused_bits     : 11;
        uint64_t cookie_mode     :  1;  // see kPolicyInCookie.
    };

    static uint32_t action(uint64_t item) { return item & kActionBits; }
    static bool is_default(uint64_t item) { return item == 0; }
};

// The packing of bits on a bitset (above) is defined by the standard as
// implementation dependent so we must check that it is using the storage
// space of a single uint64_t so the 'union' trick works.
static_assert(sizeof(Encoding) == sizeof(pol_cookie_t), "bitfield issue");

// It is critical that this array contain all "new object" policies because it's used to implement
// ZX_NEW_ANY.
const uint32_t kNewObjectPolicies[]{
    ZX_POL_NEW_VMO,
    ZX_POL_NEW_CHANNEL,
    ZX_POL_NEW_EVENT,
    ZX_POL_NEW_EVENTPAIR,
    ZX_POL_NEW_PORT,
    ZX_POL_NEW_SOCKET,
    ZX_POL_NEW_FIFO,
    ZX_POL_NEW_TIMER,
    ZX_POL_NEW_PROCESS,
    ZX_POL_NEW_PROFILE,
};
static_assert(
    fbl::count_of(kNewObjectPolicies) + 4 == ZX_POL_MAX,
    "please update JobPolicy::AddPartial, JobPolicy::QueryBasicPolicy, and kNewObjectPolicies");

bool CanSetEntry(uint64_t existing, uint32_t new_action) {
    if (Encoding::is_default(existing))
        return true;
    return (new_action == Encoding::action(existing)) ? true : false;
}

uint32_t GetEffectiveAction(uint64_t policy) {
    return Encoding::is_default(policy) ? kDefaultAction : Encoding::action(policy);
}

#define POLMAN_SET_ENTRY(mode, existing, in_pol, resultant) \
    do {                                                    \
        if (CanSetEntry(existing, in_pol)) {                \
            resultant = in_pol & Encoding::kActionBits;     \
            resultant |= Encoding::kExplicitBit;            \
        } else if (mode == ZX_JOB_POL_ABSOLUTE) {           \
            return ZX_ERR_ALREADY_EXISTS;                   \
        }                                                   \
    } while (0)

zx_status_t AddPartial(uint32_t mode, pol_cookie_t existing_policy,
                       uint32_t condition, uint32_t policy, uint64_t* partial) {
    Encoding existing = {existing_policy};
    Encoding result = {};

    if (policy >= ZX_POL_ACTION_MAX)
        return ZX_ERR_NOT_SUPPORTED;

    switch (condition) {
    case ZX_POL_BAD_HANDLE:
        POLMAN_SET_ENTRY(mode, existing.bad_handle, policy, result.bad_handle);
        break;
    case ZX_POL_WRONG_OBJECT:
        POLMAN_SET_ENTRY(mode, existing.wrong_obj, policy, result.wrong_obj);
        break;
    case ZX_POL_VMAR_WX:
        POLMAN_SET_ENTRY(mode, existing.vmar_wx, policy, result.vmar_wx);
        break;
    case ZX_POL_NEW_VMO:
        POLMAN_SET_ENTRY(mode, existing.new_vmo, policy, result.new_vmo);
        break;
    case ZX_POL_NEW_CHANNEL:
        POLMAN_SET_ENTRY(mode, existing.new_channel, policy, result.new_channel);
        break;
    case ZX_POL_NEW_EVENT:
        POLMAN_SET_ENTRY(mode, existing.new_event, policy, result.new_event);
        break;
    case ZX_POL_NEW_EVENTPAIR:
        POLMAN_SET_ENTRY(mode, existing.new_eventpair, policy, result.new_eventpair);
        break;
    case ZX_POL_NEW_PORT:
        POLMAN_SET_ENTRY(mode, existing.new_port, policy, result.new_port);
        break;
    case ZX_POL_NEW_SOCKET:
        POLMAN_SET_ENTRY(mode, existing.new_socket, policy, result.new_socket);
        break;
    case ZX_POL_NEW_FIFO:
        POLMAN_SET_ENTRY(mode, existing.new_fifo, policy, result.new_fifo);
        break;
    case ZX_POL_NEW_TIMER:
        POLMAN_SET_ENTRY(mode, existing.new_timer, policy, result.new_timer);
        break;
    case ZX_POL_NEW_PROCESS:
        POLMAN_SET_ENTRY(mode, existing.new_process, policy, result.new_process);
        break;
    case ZX_POL_NEW_PROFILE:
        POLMAN_SET_ENTRY(mode, existing.new_profile, policy, result.new_profile);
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    *partial = result.encoded;
    return ZX_OK;
}

#undef POLMAN_SET_ENTRY

}  // namespace


zx_status_t JobPolicy::AddBasicPolicy(uint32_t mode,
                                      const zx_policy_basic_t* policy_input,
                                      size_t policy_count) {
    // Don't allow overlong policies.
    if (policy_count > ZX_POL_MAX) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    uint64_t partials[ZX_POL_MAX] = {};

    // The policy computation algorithm is as follows:
    //
    //    loop over all input entries
    //        if existing item is default or same then
    //            store new policy in partial result array
    //        else if mode is absolute exit with failure
    //        else continue
    //
    // A special case is ZX_POL_NEW_ANY which applies the algorithm with
    // the same input over all ZX_NEW_ actions so that the following can
    // be expressed:
    //
    //   [0] ZX_POL_NEW_ANY     --> ZX_POL_ACTION_DENY
    //   [1] ZX_POL_NEW_CHANNEL --> ZX_POL_ACTION_ALLOW
    //
    // Which means "deny all object creation except for channel".

    zx_status_t res = ZX_OK;

    pol_cookie_t new_cookie = cookie_;

    for (size_t ix = 0; ix != policy_count; ++ix) {
        const auto& in = policy_input[ix];

        if (in.condition >= ZX_POL_MAX) {
            return ZX_ERR_INVALID_ARGS;
        }

        if (in.condition == ZX_POL_NEW_ANY) {
            // loop over all ZX_POL_NEW_xxxx conditions.
            for (auto pol : kNewObjectPolicies) {
                if ((res = AddPartial(mode, new_cookie, pol, in.policy, &partials[pol])) < 0) {
                    return res;
                }
            }
        } else {
            if ((res = AddPartial(
                     mode, new_cookie, in.condition, in.policy, &partials[in.condition])) < 0) {
                return res;
            }
        }
    }

    // Compute the resultant policy. The simple OR works because the only items that
    // can change are the items that have zero. See Encoding::is_default().
    for (const auto& partial : partials) {
        new_cookie |= partial;
    }

    cookie_ = new_cookie;
    return ZX_OK;
}

uint32_t JobPolicy::QueryBasicPolicy(uint32_t condition) const {
    if (cookie_ == kPolicyEmpty) {
        return kDefaultAction;
    }

    Encoding existing = {cookie_};
    DEBUG_ASSERT(existing.cookie_mode == Encoding::kPolicyInCookie);

    switch (condition) {
    case ZX_POL_BAD_HANDLE: return GetEffectiveAction(existing.bad_handle);
    case ZX_POL_WRONG_OBJECT: return GetEffectiveAction(existing.wrong_obj);
    case ZX_POL_NEW_VMO: return GetEffectiveAction(existing.new_vmo);
    case ZX_POL_NEW_CHANNEL: return GetEffectiveAction(existing.new_channel);
    case ZX_POL_NEW_EVENT: return GetEffectiveAction(existing.new_event);
    case ZX_POL_NEW_EVENTPAIR: return GetEffectiveAction(existing.new_eventpair);
    case ZX_POL_NEW_PORT: return GetEffectiveAction(existing.new_port);
    case ZX_POL_NEW_SOCKET: return GetEffectiveAction(existing.new_socket);
    case ZX_POL_NEW_FIFO: return GetEffectiveAction(existing.new_fifo);
    case ZX_POL_NEW_TIMER: return GetEffectiveAction(existing.new_timer);
    case ZX_POL_NEW_PROCESS: return GetEffectiveAction(existing.new_process);
    case ZX_POL_NEW_PROFILE: return GetEffectiveAction(existing.new_profile);
    case ZX_POL_VMAR_WX: return GetEffectiveAction(existing.vmar_wx);
    default: return ZX_POL_ACTION_DENY;
    }
}

void JobPolicy::SetTimerSlack(TimerSlack slack) {
    slack_ = slack;
}

TimerSlack JobPolicy::GetTimerSlack() const {
    return slack_;
}

bool JobPolicy::operator==(const JobPolicy& rhs) const {
    if (this == &rhs) {
        return true;
    }

    return cookie_ == rhs.cookie_ &&
           slack_ == rhs.slack_;
}

bool JobPolicy::operator!=(const JobPolicy& rhs) const {
    return !operator==(rhs);
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/policy_manager.h>

#include <assert.h>
#include <err.h>

#include <magenta/types.h>
#include <mxalloc/new.h>

namespace {

// The encoding of the basic policy is done 4 bits per each item.
//
// - When the top bit is 1, the lower 3 bits track the action:
//    0 : MX_POL_ACTION_ALLOW or not (MX_POL_ACTION_DENY)
//    1 : MX_POL_ACTION_ALARM or not not
//    2 : MX_POL_ACTION_KILL or not
//
// - When the top bit is 0 then its the default policy and other bits
//   should be zero so that kPolicyEmpty == 0 meets the requirement of
//   all entries being default.

union Encoding {
    static constexpr uint8_t kExplicitBit = 0b1000;
    static constexpr uint8_t kActionBits  = 0b0111;

    // Indicates the the policies are fully encoded in the cookie.
    static constexpr uint8_t kPolicyInCookie = 0;

    pol_cookie_t encoded;
    struct {
        uint64_t bad_handle      :  4;
        uint64_t wrong_obj       :  4;
        uint64_t vmar_wx         :  4;
        uint64_t new_vmo         :  4;
        uint64_t new_channel     :  4;
        uint64_t new_event       :  4;
        uint64_t new_evpair      :  4;
        uint64_t new_port        :  4;
        uint64_t new_socket      :  4;
        uint64_t new_fifo        :  4;
        uint64_t unused_bits     : 23;
        uint64_t cookie_mode     :  1;  // see kPolicyInCookie.
    };

    static uint32_t action(uint64_t item) { return item & kActionBits; }
    static bool is_default(uint64_t item) { return item == 0; }
};

}  // namespace

constexpr uint32_t kPolicyActionValidBits =
    MX_POL_ACTION_ALLOW | MX_POL_ACTION_DENY | MX_POL_ACTION_ALARM | MX_POL_ACTION_KILL;

// The packing of bits on a bitset (above) is defined by the standard as
// implementation dependent so we must check that it is using the storage
// space of a single uint64_t so the 'union' trick works.
static_assert(sizeof(Encoding) == sizeof(pol_cookie_t), "bitfield issue");

// Make sure that adding new policies forces updating this file.
static_assert(MX_POL_MAX == 11u,
    "please update PolicyManager AddPolicy and QueryBasicPolicy");

PolicyManager* PolicyManager::Create(uint32_t default_action) {
    AllocChecker ac;
    auto pm = new (&ac) PolicyManager(default_action);
    return ac.check() ? pm : nullptr;
}

PolicyManager::PolicyManager(uint32_t default_action)
    : default_action_(default_action) {
}

mx_status_t PolicyManager::AddPolicy(
    uint32_t mode, pol_cookie_t existing_policy,
    const mx_policy_basic* policy_input, size_t policy_count,
    pol_cookie_t* new_policy) {

    // Don't allow overlong policies.
    if (policy_count > MX_POL_MAX)
        return ERR_OUT_OF_RANGE;

    uint64_t partials[MX_POL_MAX] = {0};

    // The policy computation algorithm is as follows:
    //
    //    loop over all input entries
    //        if existing item is default or same then
    //            store new policy in partial result array
    //        else if mode is absolute exit with failure
    //        else continue
    //
    // A special case is MX_POL_NEW_ANY which applies the algoritm with
    // the same input over all MX_NEW_ actions so that the following can
    // be expressed:
    //
    //   [0] MX_POL_NEW_ANY     --> MX_POL_ACTION_DENY
    //   [1] MX_POL_NEW_CHANNEL --> MX_POL_ACTION_ALLOW
    //
    // Which means "deny all object creation except for channel".

    mx_status_t res = NO_ERROR;

    for (size_t ix = 0; ix != policy_count; ++ix) {
        const auto& in = policy_input[ix];

        if (in.condition >= MX_POL_MAX)
            return ERR_INVALID_ARGS;

        if (in.condition == MX_POL_NEW_ANY) {
            // loop over all MX_POL_NEW_xxxx conditions.
            for (uint32_t it = MX_POL_NEW_VMO; it <= MX_POL_NEW_FIFO; ++it) {
                if ((res = AddPartial(mode, existing_policy, it, in.policy, &partials[it])) < 0)
                    return res;
            }
        } else {
            if ((res = AddPartial(
                mode, existing_policy, in.condition, in.policy, &partials[in.condition])) < 0)
                return res;
        }
    }

    // Compute the resultant policy. The simple OR works because the only items that
    // can change are the items that have zero. See Encoding::is_default().
    for (const auto& partial : partials) {
        existing_policy |= partial;
    }

    *new_policy = existing_policy;
    return NO_ERROR;
}

uint32_t PolicyManager::QueryBasicPolicy(pol_cookie_t policy, uint32_t condition) {
    if (policy == kPolicyEmpty)
        return default_action_;

    Encoding existing = { policy };
    DEBUG_ASSERT(existing.cookie_mode == Encoding::kPolicyInCookie);

    switch (condition) {
    case MX_POL_BAD_HANDLE: return GetEffectiveAction(existing.bad_handle);
    case MX_POL_WRONG_OBJECT: return GetEffectiveAction(existing.wrong_obj);
    case MX_POL_NEW_VMO: return GetEffectiveAction(existing.new_vmo);
    case MX_POL_NEW_CHANNEL: return GetEffectiveAction(existing.new_channel);
    case MX_POL_NEW_EVENT: return GetEffectiveAction(existing.new_event);
    case MX_POL_NEW_EVPAIR: return GetEffectiveAction(existing.new_evpair);
    case MX_POL_NEW_PORT: return GetEffectiveAction(existing.new_port);
    case MX_POL_NEW_SOCKET: return GetEffectiveAction(existing.new_socket);
    case MX_POL_NEW_FIFO: return GetEffectiveAction(existing.new_fifo);
    case MX_POL_VMAR_WX: return GetEffectiveAction(existing.vmar_wx);
    default: return MX_POL_ACTION_DENY;
    };
}

uint32_t PolicyManager::GetEffectiveAction(uint64_t policy) {
    return Encoding::is_default(policy) ?
        default_action_ : Encoding::action(policy);
}

bool PolicyManager::CanSetEntry(uint64_t existing, uint32_t new_action) {
    if (Encoding::is_default(existing))
        return true;
    return (new_action == Encoding::action(existing)) ? true : false;
}

#define POLMAN_SET_ENTRY(mode, existing, in_pol, resultant)             \
    do {                                                                \
        if (CanSetEntry(existing, in_pol)) {                            \
            resultant = in_pol & Encoding::kActionBits;                 \
            resultant |= Encoding::kExplicitBit;                        \
        } else if (mode == MX_JOB_POL_ABSOLUTE) {                       \
            return ERR_ALREADY_EXISTS;                                  \
        }                                                               \
    } while (0)

mx_status_t PolicyManager::AddPartial(uint32_t mode, pol_cookie_t existing_policy,
                                      uint32_t condition, uint32_t policy, uint64_t* partial) {
    Encoding existing = { existing_policy };
    Encoding result = {0};

    if (policy & ~kPolicyActionValidBits)
        return ERR_NOT_SUPPORTED;

    switch (condition) {
    case MX_POL_BAD_HANDLE:
        POLMAN_SET_ENTRY(mode, existing.bad_handle, policy, result.bad_handle);
        break;
    case MX_POL_WRONG_OBJECT:
        POLMAN_SET_ENTRY(mode, existing.wrong_obj, policy, result.wrong_obj);
        break;
    case MX_POL_VMAR_WX:
        POLMAN_SET_ENTRY(mode, existing.vmar_wx, policy, result.vmar_wx);
        break;
    case MX_POL_NEW_VMO:
        POLMAN_SET_ENTRY(mode, existing.new_vmo, policy, result.new_vmo);
        break;
    case MX_POL_NEW_CHANNEL:
        POLMAN_SET_ENTRY(mode, existing.new_channel, policy, result.new_channel);
        break;
    case MX_POL_NEW_EVENT:
        POLMAN_SET_ENTRY(mode, existing.new_event, policy, result.new_event);
        break;
    case MX_POL_NEW_EVPAIR:
        POLMAN_SET_ENTRY(mode, existing.new_evpair, policy, result.new_evpair);
        break;
    case MX_POL_NEW_PORT:
        POLMAN_SET_ENTRY(mode, existing.new_port, policy, result.new_port);
        break;
    case MX_POL_NEW_SOCKET:
        POLMAN_SET_ENTRY(mode, existing.new_socket, policy, result.new_socket);
        break;
    case MX_POL_NEW_FIFO:
        POLMAN_SET_ENTRY(mode, existing.new_fifo, policy, result.new_fifo);
        break;
    default:
        return ERR_NOT_SUPPORTED;
    };

    *partial = result.encoded;
    return NO_ERROR;
}

#undef POLMAN_SET_ENTRY

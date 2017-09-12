// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <object/policy_manager.h>

#include <assert.h>
#include <err.h>

#include <zircon/types.h>
#include <fbl/alloc_checker.h>

namespace {

// The encoding of the basic policy is done 4 bits per each item.
//
// - When the top bit is 1, the lower 3 bits track the action:
//    0 : ZX_POL_ACTION_ALLOW or not (ZX_POL_ACTION_DENY)
//    1 : ZX_POL_ACTION_EXCEPTION or not
//    2 : ZX_POL_ACTION_KILL or not
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
        uint64_t new_timer       :  4;
        uint64_t unused_bits     : 19;
        uint64_t cookie_mode     :  1;  // see kPolicyInCookie.
    };

    static uint32_t action(uint64_t item) { return item & kActionBits; }
    static bool is_default(uint64_t item) { return item == 0; }
};

}  // namespace

constexpr uint32_t kPolicyActionValidBits =
    ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_DENY | ZX_POL_ACTION_EXCEPTION | ZX_POL_ACTION_KILL;

// The packing of bits on a bitset (above) is defined by the standard as
// implementation dependent so we must check that it is using the storage
// space of a single uint64_t so the 'union' trick works.
static_assert(sizeof(Encoding) == sizeof(pol_cookie_t), "bitfield issue");

// Make sure that adding new policies forces updating this file.
static_assert(ZX_POL_MAX == 12u, "please update PolicyManager AddPolicy and QueryBasicPolicy");

PolicyManager* PolicyManager::Create(uint32_t default_action) {
    fbl::AllocChecker ac;
    auto pm = new (&ac) PolicyManager(default_action);
    return ac.check() ? pm : nullptr;
}

PolicyManager::PolicyManager(uint32_t default_action)
    : default_action_(default_action) {
}

zx_status_t PolicyManager::AddPolicy(
    uint32_t mode, pol_cookie_t existing_policy,
    const zx_policy_basic* policy_input, size_t policy_count,
    pol_cookie_t* new_policy) {

    // Don't allow overlong policies.
    if (policy_count > ZX_POL_MAX)
        return ZX_ERR_OUT_OF_RANGE;

    uint64_t partials[ZX_POL_MAX] = {0};

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

    for (size_t ix = 0; ix != policy_count; ++ix) {
        const auto& in = policy_input[ix];

        if (in.condition >= ZX_POL_MAX)
            return ZX_ERR_INVALID_ARGS;

        if (in.condition == ZX_POL_NEW_ANY) {
            // loop over all ZX_POL_NEW_xxxx conditions.
            for (uint32_t it = ZX_POL_NEW_VMO; it <= ZX_POL_NEW_TIMER; ++it) {
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
    return ZX_OK;
}

uint32_t PolicyManager::QueryBasicPolicy(pol_cookie_t policy, uint32_t condition) {
    if (policy == kPolicyEmpty)
        return default_action_;

    Encoding existing = { policy };
    DEBUG_ASSERT(existing.cookie_mode == Encoding::kPolicyInCookie);

    switch (condition) {
    case ZX_POL_BAD_HANDLE: return GetEffectiveAction(existing.bad_handle);
    case ZX_POL_WRONG_OBJECT: return GetEffectiveAction(existing.wrong_obj);
    case ZX_POL_NEW_VMO: return GetEffectiveAction(existing.new_vmo);
    case ZX_POL_NEW_CHANNEL: return GetEffectiveAction(existing.new_channel);
    case ZX_POL_NEW_EVENT: return GetEffectiveAction(existing.new_event);
    case ZX_POL_NEW_EVPAIR: return GetEffectiveAction(existing.new_evpair);
    case ZX_POL_NEW_PORT: return GetEffectiveAction(existing.new_port);
    case ZX_POL_NEW_SOCKET: return GetEffectiveAction(existing.new_socket);
    case ZX_POL_NEW_FIFO: return GetEffectiveAction(existing.new_fifo);
    case ZX_POL_NEW_TIMER: return GetEffectiveAction(existing.new_fifo);
    case ZX_POL_VMAR_WX: return GetEffectiveAction(existing.vmar_wx);
    default: return ZX_POL_ACTION_DENY;
    }
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
        } else if (mode == ZX_JOB_POL_ABSOLUTE) {                       \
            return ZX_ERR_ALREADY_EXISTS;                               \
        }                                                               \
    } while (0)

zx_status_t PolicyManager::AddPartial(uint32_t mode, pol_cookie_t existing_policy,
                                      uint32_t condition, uint32_t policy, uint64_t* partial) {
    Encoding existing = { existing_policy };
    Encoding result = {0};

    if (policy & ~kPolicyActionValidBits)
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
    case ZX_POL_NEW_EVPAIR:
        POLMAN_SET_ENTRY(mode, existing.new_evpair, policy, result.new_evpair);
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
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    *partial = result.encoded;
    return ZX_OK;
}

#undef POLMAN_SET_ENTRY

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/policy_manager.h>

#include <err.h>
#include <new.h>

#include <magenta/syscalls/policy.h>
#include <magenta/types.h>

PolicyManager* PolicyManager::Create(PolicyAction default_action) {
    AllocChecker ac;
    auto pm = new (&ac) PolicyManager(default_action);
    return ac.check() ? pm : nullptr;
}

PolicyManager::PolicyManager(PolicyAction default_action)
    : default_action_(default_action) {}

mx_status_t PolicyManager::AddPolicy(
    uint32_t mode, pol_cookie_t existing_policy,
    const mx_policy_basic* policy_input, size_t policy_count,
    pol_cookie_t* new_policy) {

    // TODO(cpu): implement the rest.
    return ERR_NOT_SUPPORTED;
}

pol_cookie_t PolicyManager::ClonePolicy(pol_cookie_t policy) {
    // This only works for cookie-encoded policies.
    // TODO(cpu): implement.
    return policy;
}

void PolicyManager::RemovePolicy(pol_cookie_t policy) {
}

PolicyAction PolicyManager::QueryBasicPolicy(
    pol_cookie_t policy, uint32_t condition, PortDispatcher* alarm_port) {

    if (policy == kPolicyEmpty)
        return default_action_;

    // The fast path has the |policy| encoding the actual policy so
    // here there is only unpacking the bits and checking them.
    //
    // The slow path involves a lock and lookup into an array.

    return default_action_;
}

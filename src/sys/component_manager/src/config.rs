// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/71901): remove aliases once the routing lib has a stable API.
pub type RuntimeConfig = ::routing::config::RuntimeConfig;
pub type SecurityPolicy = ::routing::config::SecurityPolicy;
pub type JobPolicyAllowlists = ::routing::config::JobPolicyAllowlists;
pub type CapabilityAllowlistSource = ::routing::config::CapabilityAllowlistSource;
pub type CapabilityAllowlistKey = ::routing::config::CapabilityAllowlistKey;
pub type PolicyConfigError = ::routing::config::PolicyConfigError;
pub type AllowlistEntry = ::routing::config::AllowlistEntry;

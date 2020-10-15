// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Defines the foundational request/response types used for internal policy communications.
pub mod base;

/// Defines the policy handler trait, which describes a component that persists and applies the
/// policies specified by policy clients
pub mod policy_handler;

/// Defines a proxy between the policy FIDL handler and policy handler that is responsible for
/// intercepting incoming setting requests and returning responses to the policy FIDL handler.
pub mod policy_proxy;

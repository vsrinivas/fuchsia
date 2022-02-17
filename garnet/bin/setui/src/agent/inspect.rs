// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Agent for capturing policy state from messages from the message hub to
/// policy proxies.
pub(crate) mod policy_values;

/// Agent for capturing setting values of messages between proxies and setting
/// handlers.
pub(crate) mod setting_values;

/// Agent for writing the recent request payloads to inspect.
pub(crate) mod setting_proxy;

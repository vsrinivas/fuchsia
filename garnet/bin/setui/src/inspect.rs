// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This mod contains the inspect broker, a broker which registers with a message hub to watch
// messages between proxies and setting handlers in order to record settings values to inspect.
pub mod inspect_broker;

// This mod contains the policy inspect broker, a broker which registers with the policy message hub
// to watch messages to policy proxies in order to record policy state to inspect.
pub mod policy_inspect_broker;

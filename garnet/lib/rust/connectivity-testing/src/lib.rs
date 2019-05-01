// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

/// The connectivity-testing crate provides a set of helper functions intended to be used by
/// testing and diagnostic tools and infrastructure.  Each service type is intended to have
/// one or more support files, with the helper methods and their unit tests.
pub mod wlan_ap_service_util;
pub mod wlan_service_util;

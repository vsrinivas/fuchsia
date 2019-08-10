// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

/// The connectivity-testing crate provides a set of helper functions intended to be used by
/// testing and diagnostic tools and infrastructure.  Each service type is intended to have
/// one or more support files, with the helper methods and their unit tests.
pub mod http_service_util;
pub mod net_stack_util;
pub mod wlan_ap_service_util;
pub mod wlan_service_util;

#[cfg(test)]
fn setup_fake_service<M: fidl::endpoints::ServiceMarker>(
) -> (fuchsia_async::Executor, M::Proxy, M::RequestStream) {
    let exec = fuchsia_async::Executor::new().expect("creating executor");
    let (proxy, server) = fidl::endpoints::create_proxy::<M>().expect("creating proxy");
    (exec, proxy, server.into_stream().expect("creating stream"))
}

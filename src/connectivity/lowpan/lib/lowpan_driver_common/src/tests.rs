// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::prelude_internal::*;
use crate::DummyDevice;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan_device::DriverMarker;
use fidl_fuchsia_lowpan_thread::LegacyJoiningMarker;
use fuchsia_async as fasync;
use futures::task::{Context, Poll};

#[derive(Default, Debug)]
struct Yield(bool);

impl Future for Yield {
    type Output = ();
    fn poll(mut self: core::pin::Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        if self.as_ref().0 {
            Poll::Ready(())
        } else {
            self.as_mut().0 = true;
            Poll::Pending
        }
    }
}

#[fasync::run_until_stalled(test)]
async fn test_legacy_joining_mutual_exclusion() {
    let device = DummyDevice::default();

    let (client_ep, server_ep) =
        create_endpoints::<DriverMarker>().context("Failed to create FIDL endpoints").unwrap();

    let server_future = device.serve_to(server_ep.into_stream().unwrap());

    let client_future = async move {
        let driver_proxy = client_ep.into_proxy().unwrap();

        let (client1_ep, server1_ep) = create_endpoints::<LegacyJoiningMarker>().unwrap();

        driver_proxy
            .get_protocols(fidl_fuchsia_lowpan_device::Protocols {
                thread_legacy_joining: Some(server1_ep),
                ..fidl_fuchsia_lowpan_device::Protocols::EMPTY
            })
            .unwrap();

        let client1_proxy = client1_ep.into_proxy().unwrap();

        client1_proxy.make_joinable(0, 0).await.unwrap();

        let (client2_ep, server2_ep) = create_endpoints::<LegacyJoiningMarker>().unwrap();

        driver_proxy
            .get_protocols(fidl_fuchsia_lowpan_device::Protocols {
                thread_legacy_joining: Some(server2_ep),
                ..fidl_fuchsia_lowpan_device::Protocols::EMPTY
            })
            .unwrap();

        let client2_proxy = client2_ep.into_proxy().unwrap();

        // This should fail since server1_proxy is outstanding.
        assert!(client2_proxy.make_joinable(0, 0).await.is_err());

        client1_proxy.make_joinable(0, 0).await.unwrap();

        // Drop client1_proxy so that we can make sure we can get another.
        std::mem::drop(client1_proxy);

        // This is needed to give the server future a chance to clean itself up.
        Yield::default().await;
        Yield::default().await;

        let (client3_ep, server3_ep) = create_endpoints::<LegacyJoiningMarker>().unwrap();

        driver_proxy
            .get_protocols(fidl_fuchsia_lowpan_device::Protocols {
                thread_legacy_joining: Some(server3_ep),
                ..fidl_fuchsia_lowpan_device::Protocols::EMPTY
            })
            .unwrap();

        let client3_proxy = client3_ep.into_proxy().unwrap();

        // This should work since client1_proxy is gone.
        client3_proxy.make_joinable(0, 0).await.unwrap();
    };

    futures::select! {
        err = server_future.boxed_local().fuse() => panic!("Server task stopped: {:?}", err),
        _ = client_future.boxed().fuse() => (),
    }
}

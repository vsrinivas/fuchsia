// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::convert::TryFrom as _,
};

const NETWORK_SPEED_TEST_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/network-speed-test-test#meta/network-speed-test.cmx";

#[fasync::run_singlethreaded(test)]
async fn test_run_network_speed_test() {
    let mut fs = ServiceFs::new();

    let env = fs
        .add_fidl_service(std::convert::identity::<fidl_fuchsia_net_http::LoaderRequestStream>)
        .create_salted_nested_environment("network-speed-test_integration_test_env")
        .expect("error creating nested environment");

    let mut fs_fut = fs
        .for_each_concurrent(None /* limit */, |s| {
            s.for_each(|req| async move {
                match req.expect("expected Loader request") {
                    fidl_fuchsia_net_http::LoaderRequest::Fetch { responder, .. } => {
                        let (tx, rx) = zx::Socket::create(zx::SocketOpts::STREAM)
                            .expect("error creating stream socket");
                        let () = responder
                            .send(fidl_fuchsia_net_http::Response {
                                error: None,
                                body: Some(rx),
                                final_url: Some("http://www.test.com".to_string()),
                                status_code: Some(200),
                                status_line: Some("ok".as_bytes().to_vec()),
                                headers: None,
                                redirect: None,
                                ..fidl_fuchsia_net_http::Response::EMPTY
                            })
                            .expect("error sending response to client");
                        let mut tx = fasync::Socket::from_socket(tx)
                            .expect("error creating async socket")
                            .into_sink();

                        const BODY_SIZE: usize = 10000;
                        let () = tx
                            .send_all(&mut futures::stream::iter(0..BODY_SIZE).map(|x| {
                                Ok([u8::try_from(x % usize::from(u8::MAX))
                                    .expect("expect a value up to u8::MAX to fit in a u8")])
                            }))
                            .await
                            .expect("error sending to tx sink");
                    }
                    fidl_fuchsia_net_http::LoaderRequest::Start {
                        request: _, client: _, ..
                    } => {
                        panic!("Start is not handled")
                    }
                }
            })
        })
        .fuse();

    let mut app = client::AppBuilder::new(NETWORK_SPEED_TEST_URL)
        .arg("-u")
        .arg("http://www.test.com")
        .output(env.launcher())
        .expect("error launching app")
        .fuse();

    futures::select! {
        fs_res = fs_fut => {
            panic!("service fs unexpectedly ended; res = {:?}", fs_res);
        }
        app_res = app => {
            let output = app_res.expect("error waiting for app result");
            let () = output.ok().expect("error running network speed test");
        }
    }
}

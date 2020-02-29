// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::{client, fuchsia_single_component_package_url, server::ServiceFs},
    fuchsia_zircon as zx,
    futures::prelude::*,
};

const NETWORK_SPEED_TEST_URL: &'static str =
    fuchsia_single_component_package_url!("network-speed-test");

fn start_fake_loader(stream: fidl_fuchsia_net_http::LoaderRequestStream) {
    fasync::spawn(async move {
        stream
            .err_into()
            .try_for_each_concurrent(None, |message| async move {
                match message {
                    fidl_fuchsia_net_http::LoaderRequest::Fetch { responder, .. } => {
                        let (tx, rx) = zx::Socket::create(zx::SocketOpts::STREAM)?;
                        responder.send(fidl_fuchsia_net_http::Response {
                            error: None,
                            body: Some(rx),
                            final_url: Some("http://www.test.com".as_bytes().to_vec()),
                            status_code: Some(200),
                            status_line: Some("ok".as_bytes().to_vec()),
                            headers: None,
                            redirect: None,
                        })?;

                        fasync::spawn(async move {
                            for i in 0..100 {
                                let _ =
                                    tx.write(&std::iter::repeat(i).take(100).collect::<Vec<u8>>());
                            }
                        });

                        Ok(())
                    }
                    _ => Err(anyhow::anyhow!("Unhandled")),
                }
            })
            .await
            .unwrap()
    });
}

#[fasync::run_singlethreaded(test)]
async fn test_run_network_speed_test() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(start_fake_loader);

    let env = fs.create_salted_nested_environment("network-speed-test_integration_test_env")?;

    fasync::spawn(fs.collect());

    assert!(client::AppBuilder::new(NETWORK_SPEED_TEST_URL)
        .arg("-u")
        .arg("http://www.test.com")
        .status(env.launcher())?
        .await?
        .success());

    Ok(())
}

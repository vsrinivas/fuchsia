// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_test_conformance::{
        PuppetFactoryCreateResponse, PuppetFactoryRequest, PuppetFactoryRequestStream,
        PuppetRequestStream, Result_,
    },
    futures::TryStreamExt,
    parking_lot::Mutex,
    std::rc::Rc,
    tracing::info,
};

mod presentation_loop;
mod view;

async fn run_puppet(request_stream: PuppetRequestStream, _puppet_view: Rc<Mutex<view::View>>) {
    info!("Starting puppet instance");

    request_stream
        .try_for_each(|_request| async { Ok(()) })
        .await
        .expect("failed to serve puppet stream");

    info!("reached end of puppet request stream; closing connection");
}

pub async fn run_puppet_factory(request_stream: PuppetFactoryRequestStream) {
    info!("handling client connection to puppet factory service");

    request_stream
        .try_for_each_concurrent(None, |request| async {
            match request {
                PuppetFactoryRequest::Create { payload, responder, .. } => {
                    info!("create puppet");
                    let view_token =
                        payload.view_token.expect("missing puppet viewport creation token");
                    let puppet_server =
                        payload.puppet_server.expect("missing puppet server endpoint");

                    let view = view::View::new(view_token).await;

                    responder
                        .send(PuppetFactoryCreateResponse {
                            result: Some(Result_::Success),
                            ..PuppetFactoryCreateResponse::EMPTY
                        })
                        .expect("failed to respond to PuppetFactoryRequest::Create");

                    run_puppet(
                        puppet_server.into_stream().expect("failed to bind puppet server endpoint"),
                        view,
                    )
                    .await;
                }
            }

            Ok(())
        })
        .await
        .expect("failed to serve puppet factory stream");

    info!("reached end of puppet factory request stream; closing connection");
}

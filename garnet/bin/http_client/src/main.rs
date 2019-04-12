// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServerEnd, ServiceMarker};
use fidl_fuchsia_net_oldhttp as oldhttp;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_hyper as hyper;
use futures::compat::Future01CompatExt;
use futures::prelude::*;

fn spawn_old_url_loader(server: ServerEnd<oldhttp::UrlLoaderMarker>) {
    fasync::spawn(
        async move {
            let client = hyper::new_client();
            let c = &client;
            let stream = server.into_stream()?;
            await!(stream.err_into().try_for_each_concurrent(None, async move |message| {
                match message {
                    oldhttp::UrlLoaderRequest::Start { request, responder } => {
                        let url = request.url.parse()?;
                        let response = await!(c.get(url).compat())?;
                        responder.send(&mut oldhttp::UrlResponse {
                            status_code: response.status().as_u16() as u32,
                            body: None,
                            url: None,
                            error: None,
                            status_line: None,
                            headers: None,
                            mime_type: None,
                            charset: None,
                            redirect_method: None,
                            redirect_url: None,
                            redirect_referrer: None,
                        })?;
                    }
                    oldhttp::UrlLoaderRequest::FollowRedirect { responder: _ } => (),
                    oldhttp::UrlLoaderRequest::QueryStatus { responder: _ } => (),
                };
                Ok(())
            }))
        }
            .unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)),
    );
}

fn spawn_old_server(chan: fasync::Channel) {
    fasync::spawn(
        async move {
            let stream = oldhttp::HttpServiceRequestStream::from_channel(chan);
            await!(stream.err_into().try_for_each_concurrent(None, async move |message| {
                let oldhttp::HttpServiceRequest::CreateUrlLoader { loader, .. } = message;
                spawn_old_url_loader(loader);
                Ok(())
            }))
        }
            .unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)),
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let fut = ServicesServer::new()
        .add_service((oldhttp::HttpServiceMarker::NAME, move |chan| spawn_old_server(chan)))
        .start()
        .context("Error starting http_client services server")?;

    await!(fut).context("failed to execute http_client future")?;
    Ok(())
}

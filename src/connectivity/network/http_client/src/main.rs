// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_net_oldhttp as oldhttp;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_hyper as fhyper;
use fuchsia_zircon as zx;
use futures::compat::Future01CompatExt;
use futures::prelude::*;
use hyper;
use std::error::Error as stderr;

fn version_to_str(version: hyper::Version) -> &'static str {
    match version {
        hyper::Version::HTTP_09 => "0.9",
        hyper::Version::HTTP_10 => "1.0",
        hyper::Version::HTTP_11 => "1.1",
        hyper::Version::HTTP_2 => "2.0",
    }
}

fn to_status_line(version: hyper::Version, status: hyper::StatusCode) -> String {
    match status.canonical_reason() {
        None => format!("HTTP/{} {}", version_to_str(version), status.as_str()),
        Some(canonical_reason) => {
            format!("HTTP/{} {} {}", version_to_str(version), status.as_str(), canonical_reason)
        }
    }
}

async fn to_body(body_opt: Option<Box<oldhttp::UrlBody>>) -> Result<hyper::Body, zx::Status> {
    if let Some(body) = body_opt {
        match *body {
            oldhttp::UrlBody::Stream(socket) => {
                let stream = fasync::Socket::from_socket(socket)?
                    .into_datagram_stream()
                    .map_err(|status| Error::from(status));
                Ok(hyper::Body::wrap_stream(futures::stream::TryStreamExt::compat(stream)))
            }
            oldhttp::UrlBody::Buffer(buffer) => {
                let mut bytes = vec![0; buffer.size as usize];
                buffer.vmo.read(&mut bytes, 0)?;
                Ok(hyper::Body::from(bytes))
            }
        }
    } else {
        Ok(hyper::Body::empty())
    }
}

fn to_success_response<B>(request_url: String, resp: hyper::Response<B>) -> oldhttp::UrlResponse {
    let headers = resp
        .headers()
        .iter()
        .map(|(name, value)| oldhttp::HttpHeader {
            name: name.to_string(),
            value: String::from_utf8_lossy(value.as_bytes()).to_string(),
        })
        .collect();
    oldhttp::UrlResponse {
        status_code: resp.status().as_u16() as u32,
        // TODO: Actually return the body.
        body: None,
        url: Some(request_url),
        error: None,
        status_line: Some(to_status_line(resp.version(), resp.status())),
        headers: Some(headers),
        // TODO: Parse the MIME type from the Content-Type header. The C++
        // implementation of oldhttp doesn't parse the MIME type either.
        mime_type: None,
        // TODO: Parse the charset from the Content-Type header. The C++
        // implementation of oldhttp doesn't parse the charset either.
        charset: None,
        // TODO: Compute the redirect. The C++ implementation of oldhttp doesn't
        // compute the redirect either.
        redirect_method: None,
        redirect_url: None,
        redirect_referrer: None,
    }
}

// These codes are supposed to match http_error_list.h, but it's unclear how
// to relate these two error domains.
fn to_error_code(error: &hyper::Error) -> i32 {
    if error.is_parse() {
        -320
    } else if error.is_connect() {
        -15
    } else if error.is_canceled() {
        -3
    } else if error.is_closed() {
        -3
    } else {
        -2
    }
}

fn to_error_response(error: hyper::Error) -> oldhttp::UrlResponse {
    oldhttp::UrlResponse {
        status_code: 0,
        body: None,
        url: None,
        error: Some(Box::new(oldhttp::HttpError {
            code: to_error_code(&error),
            description: Some(error.description().to_string()),
        })),
        status_line: None,
        headers: None,
        mime_type: None,
        charset: None,
        redirect_method: None,
        redirect_url: None,
        redirect_referrer: None,
    }
}

fn spawn_old_url_loader(server: ServerEnd<oldhttp::UrlLoaderMarker>) {
    fasync::spawn(
        async move {
            let client = fhyper::new_client();
            let c = &client;
            let stream = server.into_stream()?;
            stream
                .err_into()
                .try_for_each_concurrent(None, |message| async move {
                    match message {
                        oldhttp::UrlLoaderRequest::Start { request, responder } => {
                            let mut builder = hyper::Request::builder();
                            builder.method(request.method.as_str());
                            builder.uri(&request.url);
                            if let Some(headers) = request.headers {
                                for header in &headers {
                                    builder.header(header.name.as_str(), header.value.as_str());
                                }
                            }
                            let req = builder.body(to_body(request.body).await?)?;
                            let result = c.request(req).compat().await;
                            responder.send(&mut match result {
                                Ok(resp) => to_success_response(request.url, resp),
                                Err(error) => to_error_response(error),
                            })?;
                        }
                        // TODO: Implement FollowRedirect. The C++ implementation of oldhttp doesn't
                        // follow redirects either.
                        oldhttp::UrlLoaderRequest::FollowRedirect { responder: _ } => (),
                        oldhttp::UrlLoaderRequest::QueryStatus { responder } => {
                            // TODO: We should cache the error and report it here.
                            responder.send(&mut oldhttp::UrlLoaderStatus {
                                error: None,
                                is_loading: false,
                            })?;
                        }
                    };
                    Ok(())
                })
                .await
        }
        .unwrap_or_else(|e: anyhow::Error| eprintln!("{:?}", e)),
    );
}

fn spawn_old_server(stream: oldhttp::HttpServiceRequestStream) {
    fasync::spawn(
        async move {
            stream
                .err_into()
                .try_for_each_concurrent(None, |message| async move {
                    let oldhttp::HttpServiceRequest::CreateUrlLoader { loader, .. } = message;
                    spawn_old_url_loader(loader);
                    Ok(())
                })
                .await
        }
        .unwrap_or_else(|e: anyhow::Error| eprintln!("{:?}", e)),
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(spawn_old_server);
    fs.take_and_serve_directory_handle()?;
    let () = fs.collect().await;
    Ok(())
}

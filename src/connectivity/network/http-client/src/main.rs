// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fidl::prelude::*,
    fidl_fuchsia_net_http as net_http,
    fuchsia_async::{self as fasync, TimeoutExt as _},
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    fuchsia_hyper as fhyper,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{prelude::*, StreamExt},
    hyper,
    std::convert::TryFrom,
    std::str::FromStr as _,
    tracing::{debug, error, info, trace},
};

static MAX_REDIRECTS: u8 = 10;
static DEFAULT_DEADLINE_DURATION: zx::Duration = zx::Duration::from_seconds(15);

fn to_status_line(version: hyper::Version, status: hyper::StatusCode) -> Vec<u8> {
    match status.canonical_reason() {
        None => format!("{:?} {}", version, status.as_str()),
        Some(canonical_reason) => format!("{:?} {} {}", version, status.as_str(), canonical_reason),
    }
    .as_bytes()
    .to_vec()
}

fn tcp_options() -> fhyper::TcpOptions {
    let mut options: fhyper::TcpOptions = std::default::Default::default();

    // Use TCP keepalive to notice stuck connections.
    // After 60s with no data received send a probe every 15s.
    options.keepalive_idle = Some(std::time::Duration::from_secs(60));
    options.keepalive_interval = Some(std::time::Duration::from_secs(15));
    // After 8 probes go unacknowledged treat the connection as dead.
    options.keepalive_count = Some(8);

    options
}

struct RedirectInfo {
    url: Option<hyper::Uri>,
    referrer: Option<hyper::Uri>,
    method: hyper::Method,
}

fn redirect_info(
    old_uri: &hyper::Uri,
    method: &hyper::Method,
    hyper_response: &hyper::Response<hyper::Body>,
) -> Option<RedirectInfo> {
    if hyper_response.status().is_redirection() {
        Some(RedirectInfo {
            url: hyper_response
                .headers()
                .get(hyper::header::LOCATION)
                .and_then(|loc| calculate_redirect(old_uri, loc)),
            referrer: hyper_response
                .headers()
                .get(hyper::header::REFERER)
                .and_then(|loc| calculate_redirect(old_uri, loc)),
            method: if hyper_response.status() == hyper::StatusCode::SEE_OTHER {
                hyper::Method::GET
            } else {
                method.clone()
            },
        })
    } else {
        None
    }
}

async fn to_success_response(
    current_url: &hyper::Uri,
    current_method: &hyper::Method,
    mut hyper_response: hyper::Response<hyper::Body>,
) -> Result<net_http::Response, zx::Status> {
    let redirect_info = redirect_info(current_url, current_method, &hyper_response);
    let headers = hyper_response
        .headers()
        .iter()
        .map(|(name, value)| net_http::Header {
            name: name.as_str().as_bytes().to_vec(),
            value: value.as_bytes().to_vec(),
        })
        .collect();

    let (tx, rx) = zx::Socket::create(zx::SocketOpts::STREAM)?;
    let response = net_http::Response {
        error: None,
        body: Some(rx),
        final_url: Some(current_url.to_string()),
        status_code: Some(hyper_response.status().as_u16() as u32),
        status_line: Some(to_status_line(hyper_response.version(), hyper_response.status())),
        headers: Some(headers),
        redirect: redirect_info.map(|info| net_http::RedirectTarget {
            method: Some(info.method.to_string()),
            url: info.url.map(|u| u.to_string()),
            referrer: info.referrer.map(|r| r.to_string()),
            ..net_http::RedirectTarget::EMPTY
        }),
        ..net_http::Response::EMPTY
    };

    fasync::Task::spawn(async move {
        let hyper_body = hyper_response.body_mut();
        while let Some(chunk) = hyper_body.next().await {
            if let Ok(chunk) = chunk {
                let mut offset: usize = 0;
                while offset < chunk.len() {
                    let pending = match tx.wait_handle(
                        zx::Signals::SOCKET_PEER_CLOSED | zx::Signals::SOCKET_WRITABLE,
                        zx::Time::INFINITE,
                    ) {
                        Err(status) => {
                            error!("tx.wait() failed - status: {}", status);
                            return;
                        }
                        Ok(pending) => pending,
                    };
                    if pending.contains(zx::Signals::SOCKET_PEER_CLOSED) {
                        info!("tx.wait() saw signal SOCKET_PEER_CLOSED");
                        return;
                    }
                    assert!(pending.contains(zx::Signals::SOCKET_WRITABLE));
                    let written = match tx.write(&chunk[offset..]) {
                        Err(status) => {
                            // Because of the wait above, we shouldn't ever see SHOULD_WAIT here, but to avoid
                            // brittle-ness, continue and wait again in that case.
                            if status == zx::Status::SHOULD_WAIT {
                                error!("Saw SHOULD_WAIT despite waiting first - expected now? - continuing");
                                continue;
                            }
                            info!("tx.write() failed - status: {}", status);
                            return;
                        }
                        Ok(written) => written,
                    };
                    offset += written;
                }
            }
        }
    }).detach();

    Ok(response)
}

fn to_fidl_error(error: &hyper::Error) -> net_http::Error {
    if error.is_parse() {
        net_http::Error::UnableToParse
    } else if error.is_user() {
        //TODO(zmbush): handle this case.
        net_http::Error::Internal
    } else if error.is_canceled() {
        //TODO(zmbush): handle this case.
        net_http::Error::Internal
    } else if error.is_closed() {
        net_http::Error::ChannelClosed
    } else if error.is_connect() {
        net_http::Error::Connect
    } else if error.is_incomplete_message() {
        //TODO(zmbush): handle this case.
        net_http::Error::Internal
    } else if error.is_body_write_aborted() {
        //TODO(zmbush): handle this case.
        net_http::Error::Internal
    } else {
        net_http::Error::Internal
    }
}

fn to_error_response(error: net_http::Error) -> net_http::Response {
    net_http::Response {
        error: Some(error),
        body: None,
        final_url: None,
        status_code: None,
        status_line: None,
        headers: None,
        redirect: None,
        ..net_http::Response::EMPTY
    }
}

struct Loader {
    method: hyper::Method,
    url: hyper::Uri,
    headers: hyper::HeaderMap,
    body: Vec<u8>,
    deadline: fasync::Time,
}

impl Loader {
    async fn new(req: net_http::Request) -> Result<Self, anyhow::Error> {
        let net_http::Request { method, url, headers, body, deadline, .. } = req;
        let method = method.as_ref().map(|method| hyper::Method::from_str(method)).transpose()?;
        let method = method.unwrap_or(hyper::Method::GET);
        if let Some(url) = url {
            let url = hyper::Uri::try_from(url)?;
            let headers = headers
                .unwrap_or_else(|| vec![])
                .into_iter()
                .map(|net_http::Header { name, value }| {
                    let name = hyper::header::HeaderName::from_bytes(&name)?;
                    let value = hyper::header::HeaderValue::from_bytes(&value)?;
                    Ok((name, value))
                })
                .collect::<Result<hyper::HeaderMap, anyhow::Error>>()?;

            let body = match body {
                Some(net_http::Body::Buffer(buffer)) => {
                    let mut bytes = vec![0; buffer.size as usize];
                    buffer.vmo.read(&mut bytes, 0)?;
                    bytes
                }
                Some(net_http::Body::Stream(socket)) => {
                    let mut stream = fasync::Socket::from_socket(socket)?
                        .into_datagram_stream()
                        .map(|r| r.context("reading from datagram stream"));
                    let mut bytes = Vec::new();
                    while let Some(chunk) = stream.next().await {
                        bytes.extend(chunk?);
                    }
                    bytes
                }
                None => Vec::new(),
            };

            let deadline = deadline
                .map(|deadline| fasync::Time::from_nanos(deadline))
                .unwrap_or_else(|| fasync::Time::after(DEFAULT_DEADLINE_DURATION));

            trace!("Starting request {} {}", method, url);

            Ok(Loader { method, url, headers, body, deadline })
        } else {
            Err(anyhow::Error::msg("Request missing URL"))
        }
    }

    fn build_request(&self) -> hyper::Request<hyper::Body> {
        let Self { method, url, headers, body, deadline: _ } = self;
        let mut request = hyper::Request::new(body.clone().into());
        *request.method_mut() = method.clone();
        *request.uri_mut() = url.clone();
        *request.headers_mut() = headers.clone();
        request
    }

    async fn start(mut self, loader_client: net_http::LoaderClientProxy) -> Result<(), zx::Status> {
        let client = fhyper::new_https_client_from_tcp_options(tcp_options());
        loop {
            break match client.request(self.build_request()).await {
                Ok(hyper_response) => {
                    let redirect = redirect_info(&self.url, &self.method, &hyper_response);
                    if let Some(redirect) = redirect {
                        if let Some(url) = redirect.url {
                            self.url = url;
                            self.method = redirect.method;
                            trace!(
                                "Reporting redirect to OnResponse: {} {}",
                                self.method,
                                self.url
                            );
                            let response =
                                to_success_response(&self.url, &self.method, hyper_response)
                                    .await?;
                            match loader_client.on_response(response).await {
                                Ok(()) => {}
                                Err(e) => {
                                    debug!("Not redirecting because: {}", e);
                                    break Ok(());
                                }
                            };
                            trace!("Redirect allowed to {} {}", self.method, self.url);
                            continue;
                        }
                    }
                    let response =
                        to_success_response(&self.url, &self.method, hyper_response).await?;
                    // We don't care if on_response returns an error since this is the last
                    // callback.
                    let _: Result<_, _> = loader_client.on_response(response).await;
                    Ok(())
                }
                Err(error) => {
                    info!("Received network level error from hyper: {}", error);
                    // We don't care if on_response returns an error since this is the last
                    // callback.
                    let _: Result<_, _> =
                        loader_client.on_response(to_error_response(to_fidl_error(&error))).await;
                    Ok(())
                }
            };
        }
    }

    async fn fetch(
        mut self,
    ) -> Result<(hyper::Response<hyper::Body>, hyper::Uri, hyper::Method), net_http::Error> {
        let deadline = self.deadline;
        if deadline < fasync::Time::now() {
            return Err(net_http::Error::DeadlineExceeded);
        }
        let client = fhyper::new_https_client_from_tcp_options(tcp_options());

        async move {
            let mut redirects = 0;
            loop {
                break match client.request(self.build_request()).await {
                    Ok(hyper_response) => {
                        if redirects != MAX_REDIRECTS {
                            let redirect = redirect_info(&self.url, &self.method, &hyper_response);
                            if let Some(redirect) = redirect {
                                if let Some(url) = redirect.url {
                                    self.url = url;
                                    self.method = redirect.method;
                                    trace!("Redirecting to {} {}", self.method, self.url);
                                    redirects += 1;
                                    continue;
                                }
                            }
                        }
                        Ok((hyper_response, self.url, self.method))
                    }
                    Err(e) => {
                        info!("Received network level error from hyper: {}", e);
                        Err(to_fidl_error(&e))
                    }
                };
            }
        }
        .on_timeout(deadline, || Err(net_http::Error::DeadlineExceeded))
        .await
    }
}

fn calculate_redirect(
    old_url: &hyper::Uri,
    location: &hyper::header::HeaderValue,
) -> Option<hyper::Uri> {
    let old_parts = old_url.clone().into_parts();
    let mut new_parts = hyper::Uri::try_from(location.as_bytes()).ok()?.into_parts();
    if new_parts.scheme.is_none() {
        new_parts.scheme = old_parts.scheme;
    }
    if new_parts.authority.is_none() {
        new_parts.authority = old_parts.authority;
    }
    Some(hyper::Uri::from_parts(new_parts).ok()?)
}

fn spawn_server(stream: net_http::LoaderRequestStream) {
    fasync::Task::spawn(
        async move {
            stream
                .err_into()
                .try_for_each_concurrent(None, |message| async move {
                    match message {
                        net_http::LoaderRequest::Fetch { request, responder } => {
                            debug!(
                                "Fetch request received (url: {}): {:?}",
                                request
                                    .url
                                    .as_ref()
                                    .and_then(|url| Some(url.as_str()))
                                    .unwrap_or_default(),
                                request
                            );
                            let result = Loader::new(request).await?.fetch().await;
                            responder.send(match result {
                                Ok((hyper_response, final_url, final_method)) => {
                                    to_success_response(&final_url, &final_method, hyper_response)
                                        .await?
                                }
                                Err(error) => to_error_response(error),
                            })?;
                        }
                        net_http::LoaderRequest::Start { request, client, control_handle } => {
                            debug!(
                                "Start request received (url: {}): {:?}",
                                request
                                    .url
                                    .as_ref()
                                    .and_then(|url| Some(url.as_str()))
                                    .unwrap_or_default(),
                                request
                            );
                            Loader::new(request).await?.start(client.into_proxy()?).await?;
                            control_handle.shutdown();
                        }
                    }
                    Ok(())
                })
                .await
        }
        .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
    )
    .detach();
}

#[fuchsia::component]
async fn main() -> Result<(), anyhow::Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> = fs.dir("svc").add_fidl_service(spawn_server);
    let _: &mut ServiceFs<_> = fs.take_and_serve_directory_handle()?;
    let () = fs.collect().await;
    Ok(())
}

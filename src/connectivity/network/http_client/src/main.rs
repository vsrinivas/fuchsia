// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_net_http as net_http, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_hyper as fhyper,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{
        compat::{Future01CompatExt, Stream01CompatExt},
        future::BoxFuture,
        prelude::*,
        StreamExt,
    },
    http::HttpTryFrom,
    hyper,
    log::{debug, error, info, trace},
};

static LOG_VERBOSITY: u16 = 1;
static MAX_REDIRECTS: u8 = 10;

fn version_to_str(version: hyper::Version) -> &'static str {
    match version {
        hyper::Version::HTTP_09 => "0.9",
        hyper::Version::HTTP_10 => "1.0",
        hyper::Version::HTTP_11 => "1.1",
        hyper::Version::HTTP_2 => "2.0",
    }
}

fn to_status_line(version: hyper::Version, status: hyper::StatusCode) -> Vec<u8> {
    match status.canonical_reason() {
        None => format!("HTTP/{} {}", version_to_str(version), status.as_str()).as_bytes().to_vec(),
        Some(canonical_reason) => {
            format!("HTTP/{} {} {}", version_to_str(version), status.as_str(), canonical_reason)
                .as_bytes()
                .to_vec()
        }
    }
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
        final_url: Some(current_url.to_string().as_bytes().to_vec()),
        status_code: Some(hyper_response.status().as_u16() as u32),
        status_line: Some(to_status_line(hyper_response.version(), hyper_response.status())),
        headers: Some(headers),
        redirect: redirect_info.map(|info| net_http::RedirectTarget {
            method: Some(info.method.to_string()),
            url: info.url.map(|u| u.to_string().as_bytes().to_vec()),
            referrer: info.referrer.map(|r| r.to_string().as_bytes().to_vec()),
        }),
    };

    fasync::spawn(async move {
        let mut hyper_body = hyper_response.body_mut().compat();
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
    });

    Ok(response)
}

fn to_fidl_error(error: &hyper::Error) -> net_http::Error {
    // TODO(zmbush): Handle more error cases when hyper is updated.
    if error.is_parse() {
        net_http::Error::UnableToParse
    } else if error.is_closed() {
        net_http::Error::ChannelClosed
    } else if error.is_connect() {
        net_http::Error::Connect
    } else {
        net_http::Error::Internal
    }
}

fn to_error_response(error: hyper::Error) -> net_http::Response {
    net_http::Response {
        error: Some(to_fidl_error(&error)),
        body: None,
        final_url: None,
        status_code: None,
        status_line: None,
        headers: None,
        redirect: None,
    }
}

struct Loader {
    method: hyper::Method,
    url: hyper::Uri,
    headers: hyper::HeaderMap,
    body: Vec<u8>,
}

impl Loader {
    async fn new(req: net_http::Request) -> Result<Self, Error> {
        let method =
            hyper::Method::from_bytes(req.method.unwrap_or_else(|| "GET".to_string()).as_bytes())?;
        if let Some(url) = req.url {
            let url = hyper::Uri::try_from(String::from_utf8(url)?)?;
            let mut headers = hyper::HeaderMap::new();
            if let Some(h) = req.headers {
                for header in &h {
                    headers.insert(
                        hyper::header::HeaderName::from_bytes(&header.name)?,
                        hyper::header::HeaderValue::from_bytes(&header.value)?,
                    );
                }
            }

            let body = match req.body {
                Some(net_http::Body::Buffer(buffer)) => {
                    let mut bytes = vec![0; buffer.size as usize];
                    buffer.vmo.read(&mut bytes, 0)?;
                    bytes
                }
                Some(net_http::Body::Stream(socket)) => {
                    let mut stream = fasync::Socket::from_socket(socket)?
                        .into_datagram_stream()
                        .map_err(|status| Error::from(status));
                    let mut bytes = Vec::new();
                    while let Some(chunk) = stream.next().await {
                        bytes.extend(chunk?);
                    }
                    bytes
                }
                None => Vec::new(),
            };

            trace!("Starting request {} {}", method, url);

            Ok(Loader { method, url, headers, body })
        } else {
            Err(Error::msg("Request missing URL"))
        }
    }

    fn build_request(&self) -> Result<hyper::Request<hyper::Body>, http::Error> {
        let mut builder = hyper::Request::builder();
        builder.method(&self.method);
        builder.uri(&self.url);
        for (name, value) in &self.headers {
            builder.header(name, value);
        }
        builder.body(self.body.clone().into())
    }

    fn start(
        mut self,
        loader_client: net_http::LoaderClientProxy,
    ) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            let client = fhyper::new_https_client();
            let hyper_response = match client.request(self.build_request()?).compat().await {
                Ok(response) => response,
                Err(error) => {
                    info!("Received network level error from hyper: {}", error);
                    // We don't care if on_response never returns, since this is the last callback.
                    let _ = loader_client.on_response(to_error_response(error)).await;
                    return Ok(());
                }
            };
            let redirect = redirect_info(&self.url, &self.method, &hyper_response);

            if let Some(redirect) = redirect {
                if let Some(url) = redirect.url {
                    self.url = url;
                    self.method = redirect.method;
                    trace!("Reporting redirect to OnResponse: {} {}", self.method, self.url);
                    match loader_client
                        .on_response(
                            to_success_response(&self.url, &self.method, hyper_response).await?,
                        )
                        .await
                    {
                        Err(e) => {
                            debug!("Not redirecting because: {}", e);
                            return Ok(());
                        }
                        _ => {}
                    }
                    trace!("Redirect allowed to {} {}", self.method, self.url);
                    self.start(loader_client).await?;

                    return Ok(());
                }
            }

            // We don't care if on_response never returns, since this is the last callback.
            let _ = loader_client
                .on_response(to_success_response(&self.url, &self.method, hyper_response).await?)
                .await;

            Ok(())
        }
        .boxed()
    }

    fn fetch(
        mut self,
        redirects_remaining: u8,
    ) -> BoxFuture<
        'static,
        Result<
            Result<(hyper::Response<hyper::Body>, hyper::Uri, hyper::Method), hyper::Error>,
            http::Error,
        >,
    > {
        async move {
            let client = fhyper::new_https_client();
            let result = client.request(self.build_request()?).compat().await;

            Ok(match result {
                Ok(hyper_response) => {
                    if redirects_remaining > 0 {
                        let redirect = redirect_info(&self.url, &self.method, &hyper_response);
                        if let Some(redirect) = redirect {
                            if let Some(url) = redirect.url {
                                self.url = url;
                                self.method = redirect.method;
                                trace!("Redirecting to {} {}", self.method, self.url);
                                return self.fetch(redirects_remaining - 1).await;
                            }
                        }
                    }
                    Ok((hyper_response, self.url, self.method))
                }
                Err(e) => {
                    info!("Received network level error from hyper: {}", e);
                    Err(e)
                }
            })
        }
        .boxed()
    }
}

fn calculate_redirect(
    old_url: &hyper::Uri,
    location: &hyper::header::HeaderValue,
) -> Option<hyper::Uri> {
    let old_parts = old_url.clone().into_parts();
    let mut new_parts = hyper::Uri::try_from(location.to_str().ok()?).ok()?.into_parts();
    if new_parts.scheme.is_none() {
        new_parts.scheme = old_parts.scheme;
    }
    if new_parts.authority.is_none() {
        new_parts.authority = old_parts.authority;
    }
    Some(hyper::Uri::from_parts(new_parts).ok()?)
}

fn spawn_server(stream: net_http::LoaderRequestStream) {
    fasync::spawn(
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
                                    .and_then(|url| String::from_utf8(url.to_vec()).ok())
                                    .unwrap_or_else(|| String::new()),
                                request
                            );
                            let result = Loader::new(request).await?.fetch(MAX_REDIRECTS).await?;
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
                                    .and_then(|url| String::from_utf8(url.to_vec()).ok())
                                    .unwrap_or_else(|| String::new()),
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
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init()?;
    fuchsia_syslog::set_verbosity(LOG_VERBOSITY);
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(spawn_server);
    fs.take_and_serve_directory_handle()?;
    let () = fs.collect().await;
    Ok(())
}

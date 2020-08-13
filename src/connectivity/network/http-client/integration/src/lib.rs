// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl::{endpoints::RequestStream, handle::fuchsia_handles::Channel},
    fidl_fuchsia_net_http as http, fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher, App},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::prelude::*,
    rouille::{self, router, Request, Response},
    std::{thread, time},
};

const SERVER_IP: &str = "[::]";
const ROOT_DOCUMENT: &str = "Root document\n";
const BIG_STREAM_SIZE: usize = 32usize * 1024 * 1024;

pub fn serve_request(request: &Request) -> Response {
    router!(request,
        (GET) (/) => {
            rouille::Response::text(ROOT_DOCUMENT)
        },
        (GET) (/trigger_301) => {
            rouille::Response::redirect_301("/")
        },
        (POST) (/see_other) => {
            rouille::Response::redirect_303("/")
        },
        (GET) (/loop1) => {
            rouille::Response::redirect_301("/loop2")
        },
        (GET) (/loop2) => {
            rouille::Response::redirect_301("/loop1")
        },
        (GET) (/responds_in_10_minutes) => {
            thread::sleep(time::Duration::from_millis(600_000));
            rouille::Response::text(ROOT_DOCUMENT)
        },
        (GET) (/big_stream) => {
            let mut big_vec: Vec<u8> = Vec::with_capacity(BIG_STREAM_SIZE);
            for n in 0..BIG_STREAM_SIZE {
                big_vec.push((n % 256usize) as u8);
            }
            rouille::Response::from_data("application/octet-stream", big_vec)
        },
        _ => {
            rouille::Response::text("File not found\n").with_status_code(404)
        }
    )
}

fn setup() -> Result<(u16, App, http::LoaderProxy), Error> {
    let address = format!("{}:0", SERVER_IP);
    let server =
        rouille::Server::new(address, serve_request).expect("failed to create rouille server");
    let server_port = server.server_addr().port();
    thread::Builder::new().name("test-server".into()).spawn(move || {
        server.run();
    })?;

    let launcher = launcher().context("Failed to open launcher service")?;
    let http_client = launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/http-client-integration-test#meta/http-client.cmx".to_string(),
        None,
    )
    .context("Failed to launch http_client")?;

    let loader = http_client.connect_to_service::<http::LoaderMarker>()?;

    Ok((server_port, http_client, loader))
}

fn make_url(port: u16, path: &str) -> String {
    format!("http://127.0.0.1:{}{}", port, path)
}

fn make_request(method: &str, url: String) -> http::Request {
    http::Request {
        url: Some(url),
        method: Some(method.to_string()),
        headers: None,
        body: None,
        deadline: None,
    }
}

fn check_response_common(response: &http::Response, expected_header_names: &[&str]) {
    assert_eq!(response.status_code.unwrap(), 200);
    // If the webserver started above ever returns different headers, or changes the order, this
    // assertion will fail. Note that case doesn't matter, and can vary across HTTP client
    // implementations, so we lowercase all the header keys before checking.
    let response_headers: Vec<String> = response
        .headers
        .as_ref()
        .unwrap()
        .iter()
        .map(|h| std::str::from_utf8(&h.name).unwrap().to_lowercase())
        .collect();
    assert_eq!(&response_headers, &expected_header_names);
}

fn check_response(response: &http::Response) {
    check_response_common(response, &["server", "date", "content-type", "content-length"]);
}

fn check_response_big(response: &http::Response) {
    check_response_common(response, &["server", "date", "content-type", "transfer-encoding"]);
}

fn check_body(body: Option<zx::Socket>) {
    let body = body.unwrap();

    // Temporary check until we can use const fn when defining buf below.
    assert_eq!(ROOT_DOCUMENT.len(), 14);
    let mut buf = [0; 14];
    loop {
        match body.read(&mut buf) {
            Ok(_) => break,
            Err(zx::Status::SHOULD_WAIT) => {}
            _ => panic!("This state should not be reachable"),
        }
    }
    assert_eq!(&buf, ROOT_DOCUMENT.as_bytes());
}

fn check_body_big(body: Option<zx::Socket>) {
    let body = body.unwrap();
    let mut c: u8 = 0;
    let mut buf_count = 0;
    const BUF_SIZE: usize = 1024;
    let mut buf = [0u8; BUF_SIZE];
    const MIN_SLEEPS: usize = 16;
    loop {
        if buf_count % (BIG_STREAM_SIZE / BUF_SIZE / MIN_SLEEPS) == 0 {
            // Don't read too fast - let the sender be forced to see some partial writes.
            thread::sleep(time::Duration::from_millis(100));
        }
        buf_count += 1;
        match body.read(&mut buf) {
            Ok(bytes_read) => {
                for n in 0..bytes_read {
                    assert_eq!(c, buf[n]);
                    c = ((c as u32) + 1) as u8;
                }
            }
            Err(zx::Status::SHOULD_WAIT) => {
                match body.wait_handle(
                    zx::Signals::SOCKET_READABLE | zx::Signals::SOCKET_PEER_CLOSED,
                    zx::Time::INFINITE,
                ) {
                    Err(status) => {
                        // complain about the specific status
                        assert_eq!(zx::Status::OK, status);
                    }
                    Ok(_) => {}
                };
            }
            Err(zx::Status::PEER_CLOSED) => {
                break;
            }
            Err(status) => {
                // unexpected error
                assert_eq!(zx::Status::OK, status);
            }
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_http() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request("GET", make_url(server_port, "/"))).await?;

    check_response(&response);
    check_body(response.body);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_past_deadline() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader
        .fetch({
            let mut req = make_request("GET", make_url(server_port, "/"));

            // Deadline expired 10 minutes ago!
            req.deadline = Some(zx::Time::after(zx::Duration::from_minutes(-10)).into_nanos());
            req
        })
        .await?;

    assert_eq!(response.error, Some(http::Error::DeadlineExceeded));
    assert!(response.body.is_none());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_response_too_slow() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    // But it doesn't when the deadline is 100ms in the future.
    let response = loader
        .fetch({
            let mut req = make_request("GET", make_url(server_port, "/responds_in_10_minutes"));
            // Deadline expires 100ms from now.
            req.deadline = Some(zx::Time::after(zx::Duration::from_millis(100)).into_nanos());
            req
        })
        .await?;

    assert_eq!(response.error.unwrap(), http::Error::DeadlineExceeded);
    assert!(response.body.is_none());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_https() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response =
        loader.fetch(make_request("GET", format!("https://127.0.0.1:{}", server_port))).await?;

    // Using fhyper::new_client, the request will actually ignore the https:// completely and make
    // the request successfully anyway (Since rouille doesn't care about https either). However,
    // when we use fhyper::new_https_client, the request will be dropped with a Connect error
    // because this isn't a valid https request.
    assert_eq!(response.error.unwrap(), http::Error::Connect);
    assert!(response.body.is_none());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_start_http() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let (tx, rx) = Channel::create()?;
    let mut rs = http::LoaderClientRequestStream::from_channel(fasync::Channel::from_channel(rx)?);

    loader.start(make_request("GET", make_url(server_port, "/")), tx.into())?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    check_response(&response);
    check_body(response.body);

    responder.send()?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_redirect() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request("GET", make_url(server_port, "/trigger_301"))).await?;

    check_response(&response);

    assert_eq!(response.final_url.unwrap(), make_url(server_port, "/"));

    check_body(response.body);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_start_redirect() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let (tx, rx) = Channel::create()?;
    let mut rs = http::LoaderClientRequestStream::from_channel(fasync::Channel::from_channel(rx)?);

    loader.start(make_request("GET", make_url(server_port, "/trigger_301")), tx.into())?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    assert_eq!(response.status_code.unwrap(), 301);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");
    assert_eq!(redirect.url.unwrap(), make_url(server_port, "/"));

    responder.send()?;

    let (response, _) = rs.next().await.unwrap()?.into_on_response().unwrap();
    check_response(&response);
    assert_eq!(response.final_url.unwrap(), make_url(server_port, "/"));
    check_body(response.body);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_see_other() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request("POST", make_url(server_port, "/see_other"))).await?;

    check_response(&response);

    assert_eq!(response.final_url.unwrap(), make_url(server_port, "/"));

    check_body(response.body);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_start_see_other() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let (tx, rx) = Channel::create()?;
    let mut rs = http::LoaderClientRequestStream::from_channel(fasync::Channel::from_channel(rx)?);

    loader.start(make_request("POST", make_url(server_port, "/see_other")), tx.into())?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    assert_eq!(response.status_code.unwrap(), 303);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");
    assert_eq!(redirect.url.unwrap(), make_url(server_port, "/"));

    responder.send()?;

    let (response, _) = rs.next().await.unwrap()?.into_on_response().unwrap();
    check_response(&response);
    assert_eq!(response.final_url.unwrap(), make_url(server_port, "/"));
    check_body(response.body);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_max_redirect() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request("GET", make_url(server_port, "/loop1"))).await?;

    // The last request in the redirect loop will always return status code 301
    assert_eq!(response.status_code.unwrap(), 301);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_start_redirect_loop() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let (tx, rx) = Channel::create()?;
    let mut rs = http::LoaderClientRequestStream::from_channel(fasync::Channel::from_channel(rx)?);

    loader.start(make_request("GET", make_url(server_port, "/loop1")), tx.into())?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    assert_eq!(response.status_code.unwrap(), 301);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");
    assert_eq!(redirect.url.unwrap(), make_url(server_port, "/loop2"));

    responder.send()?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    assert_eq!(response.status_code.unwrap(), 301);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");
    assert_eq!(redirect.url.unwrap(), make_url(server_port, "/loop1"));

    responder.send()?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    assert_eq!(response.status_code.unwrap(), 301);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");
    assert_eq!(redirect.url.unwrap(), make_url(server_port, "/loop2"));

    responder.send()?;

    let (response, _) = rs.next().await.unwrap()?.into_on_response().unwrap();
    assert_eq!(response.status_code.unwrap(), 301);

    let redirect = response.redirect.unwrap();
    assert_eq!(redirect.method.unwrap(), "GET");
    assert_eq!(redirect.url.unwrap(), make_url(server_port, "/loop1"));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_http_big_stream() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request("GET", make_url(server_port, "/big_stream"))).await?;

    check_response_big(&response);
    check_body_big(response.body);

    Ok(())
}

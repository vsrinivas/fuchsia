// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl::{endpoints::RequestStream, handle::fuchsia_handles::Channel},
    fidl_fuchsia_net_http as http, fuchsia_async as fasync,
    fuchsia_component::{
        client::{launch, launcher, App},
        fuchsia_single_component_package_url,
    },
    fuchsia_zircon as zx,
    futures::prelude::*,
    rouille::{self, router, Request, Response},
    std::thread,
};

const SERVER_IP: &str = "[::]";
const ROOT_DOCUMENT: &str = "Root document\n";

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
    let http_client =
        launch(&launcher, fuchsia_single_component_package_url!("http_client").to_string(), None)
            .context("Failed to launch http_client")?;

    let loader = http_client.connect_to_service::<http::LoaderMarker>()?;

    Ok((server_port, http_client, loader))
}

fn make_url(port: u16, path: &str) -> Vec<u8> {
    format!("http://127.0.0.1:{}{}", port, path).as_bytes().to_vec()
}

fn make_request(port: u16, method: &str, path: &str) -> http::Request {
    http::Request {
        url: Some(make_url(port, path)),
        method: Some(method.to_string()),
        headers: None,
        body: None,
    }
}

fn check_response(response: &http::Response) {
    assert_eq!(response.status_code.unwrap(), 200);
    let expected_header_names = ["server", "date", "content-type", "content-length"];
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

#[fasync::run_singlethreaded(test)]
async fn test_fetch_http() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request(server_port, "GET", "/")).await?;

    check_response(&response);
    check_body(response.body);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_start_http() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let (tx, rx) = Channel::create()?;
    let mut rs = http::LoaderClientRequestStream::from_channel(fasync::Channel::from_channel(rx)?);

    loader.start(make_request(server_port, "GET", "/"), tx.into())?;

    let (response, responder) = rs.next().await.unwrap()?.into_on_response().unwrap();
    check_response(&response);
    check_body(response.body);

    responder.send()?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_redirect() -> Result<(), Error> {
    let (server_port, _http_client, loader) = setup()?;

    let response = loader.fetch(make_request(server_port, "GET", "/trigger_301")).await?;

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

    loader.start(make_request(server_port, "GET", "/trigger_301"), tx.into())?;

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

    let response = loader.fetch(make_request(server_port, "POST", "/see_other")).await?;

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

    loader.start(make_request(server_port, "POST", "/see_other"), tx.into())?;

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

    let response = loader.fetch(make_request(server_port, "GET", "/loop1")).await?;

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

    loader.start(make_request(server_port, "GET", "/loop1"), tx.into())?;

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

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use anyhow::Error;
use argh::FromArgs;
use diagnostics_stream::parse::parse_record as parse;
use fidl_fuchsia_logger::LogSinkRequest;
use fidl_fuchsia_logger::LogSinkRequestStream;
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fuchsia_async as fasync;
use fuchsia_async::Socket;
use fuchsia_async::Task;
use fuchsia_component::client::App;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::channel::mpsc::channel;
use futures::channel::mpsc::Receiver;
use futures::channel::mpsc::Sender;
use futures::prelude::*;
use log::*;

/// Validate Log VMO formats written by 'puppet' programs controlled by
/// this Validator program.
#[derive(Debug, FromArgs)]
struct Opt {
    /// required arg: The URL of the puppet
    #[argh(option, long = "url")]
    puppet_url: String,
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).unwrap();
    let Opt { puppet_url } = argh::from_env();
    let (tx, mut rx): (Sender<fidl::Socket>, Receiver<fidl::Socket>) = channel(1);

    let (_env, _app) = launch_puppet(&puppet_url, tx)?;

    let socket = Socket::from_socket(rx.next().await.unwrap()).unwrap();
    test_socket(&socket, puppet_url).await;

    Ok(())
}

pub async fn test_socket(s: &fasync::Socket, puppet_url: String) {
    info!("Running the LogSink socket test.");

    let mut buf: Vec<u8> = vec![];
    // TODO(fxbug.dev/61495): Validate that this is in fact a datagram socket.
    let bytes_read = s.read_datagram(&mut buf).await.unwrap();
    let result = parse(&buf[0..bytes_read]).unwrap();
    assert_eq!(result.0.arguments[0].name, "pid");
    assert_eq!(result.0.arguments[1].name, "tid");
    assert_eq!(result.0.arguments[2].name, "tag");
    assert!(
        matches!(&result.0.arguments[2].value, diagnostics_stream::Value::Text(v) if *v == puppet_url.rsplit('/').next().unwrap())
    );
    // TODO(fxbug.dev/61538) validate we can log arbitrary messages
    assert_eq!(result.0.arguments[3].name, "tag");
    assert!(
        matches!(&result.0.arguments[3].value, diagnostics_stream::Value::Text(v) if *v == "test_log")
    );
    assert_eq!(result.0.arguments[4].name, "foo");
    assert!(
        matches!(&result.0.arguments[4].value, diagnostics_stream::Value::Text(v) if *v == "bar")
    );

    info!("Tested LogSink socket successfully.");
}

pub fn launch_puppet(
    puppet_url: &str,
    tx: Sender<zx::Socket>,
) -> Result<(Option<(EnvironmentControllerProxy, Task<()>)>, App), Error> {
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(IncomingRequest::LogProviderRequest);
    let (_env, app) = fs
        .launch_component_in_nested_environment(puppet_url.to_owned(), None, "log_validator_puppet")
        .unwrap();
    // Wait for the puppet to connect to our fake log service
    fs.take_and_serve_directory_handle()?;
    let _future = Task::spawn(async move {
        while let Some(IncomingRequest::LogProviderRequest(stream)) = fs.next().await {
            let tx_local = tx.clone();
            retrieve_sockets_from_logsink(stream, tx_local)
                .await
                .context("couldn't retrieve sockets")
                .unwrap();
        }
    });
    Ok((Some((_env, _future)), app))
}

enum IncomingRequest {
    LogProviderRequest(LogSinkRequestStream),
}

async fn retrieve_sockets_from_logsink(
    mut stream: LogSinkRequestStream,
    mut channel: Sender<fidl::Socket>,
) -> Result<(), Error> {
    let request = stream.next().await;
    match request {
        Some(Ok(LogSinkRequest::Connect { socket: _, control_handle: _ })) => {
            panic!("shouldn't ever receive legacy connections");
        }
        Some(Ok(LogSinkRequest::ConnectStructured { socket, control_handle: _ })) => {
            info!("This happened! We got a structured connection.");
            channel.send(socket).await?;
        }
        None => (),
        Some(Err(e)) => panic!("log sink request failure: {:?}", e),
    }

    Ok(())
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    clap::{App, Arg, SubCommand},
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    fidl_fuchsia_overnet_socketpassingexample as socketpassing,
    futures::prelude::*,
};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("overnet-socket-passing")
        .version("0.1.0")
        .about("Interface passing example for overnet")
        .author("Fuchsia Team")
        .subcommand(SubCommand::with_name("client").about("Run as client").arg(
            Arg::with_name("text").help("Text string to echo back and forth").takes_value(true),
        ))
        .subcommand(SubCommand::with_name("server").about("Run as server"))
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(text: &str) -> Result<(), Error> {
    let svc = hoist::connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        println!("Got peers: {:?}", peers);
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == socketpassing::ExampleMarker::NAME)
                .is_none()
            {
                continue;
            }
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            if let Err(e) =
                svc.connect_to_service(&mut peer.id, socketpassing::ExampleMarker::NAME, s)
            {
                println!("{:?}", e);
                continue;
            }
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = socketpassing::ExampleProxy::new(proxy);

            let (ss, cs) = fidl::Socket::create(fidl::SocketOpts::STREAM)
                .context("failed to create socket")?;
            println!("Sending {:?} to {:?}", text, peer.id);
            if let Err(e) = cli.pass(ss) {
                println!("ERROR PASSING SOCKET: {:?}", e);
                continue;
            }
            cs.write(text.as_bytes())?;
            let mut cs = fidl::AsyncSocket::from_socket(cs)?;
            let mut incoming = Vec::new();
            while incoming.len() != text.as_bytes().len() {
                let mut buf = [0u8; 128];
                let n = cs.read(&mut buf).await?;
                incoming.extend_from_slice(&buf[..n]);
            }
            assert_eq!(incoming, text.as_bytes());
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

fn spawn_echo_server(socket: fidl::Socket, quiet: bool) {
    hoist::spawn(
        async move {
            let mut buf = [0u8; 1024];
            let mut socket = fidl::AsyncSocket::from_socket(socket)?;
            loop {
                let n = socket.read(&mut buf).await?;
                if !quiet {
                    println!("got bytes: {:?}", &buf[..n]);
                }
                socket.write(&buf[..n]).await?;
            }
        }
        .unwrap_or_else(|e: Error| eprintln!("{:?}", e)),
    );
}

fn spawn_example_server(chan: fidl::AsyncChannel, quiet: bool) {
    hoist::spawn(
        async move {
            let mut stream = socketpassing::ExampleRequestStream::from_channel(chan);
            while let Some(request) =
                stream.try_next().await.context("error running echo server")?
            {
                match request {
                    socketpassing::ExampleRequest::Pass { socket, .. } => {
                        if !quiet {
                            println!("Received socket request");
                        }
                        spawn_echo_server(socket, quiet);
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: Error| eprintln!("{:?}", e)),
    );
}

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>, Error> {
    println!("Awaiting request");
    Ok(stream.try_next().await.context("error running service provider server")?)
}

async fn exec_server(quiet: bool) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    hoist::publish_service(socketpassing::ExampleMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        chan,
        info: _,
        control_handle: _control_handle,
    }) = next_request(&mut stream).await?
    {
        if !quiet {
            println!("Received service request for service");
        }
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        spawn_example_server(chan, quiet);
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    std::env::set_var("RUST_BACKTRACE", "full");
    let args = app().get_matches();

    match args.subcommand() {
        ("server", Some(_)) => exec_server(args.is_present("quiet")).await,
        ("client", Some(cmd)) => {
            let r = exec_client(cmd.value_of("text").unwrap_or("")).await;
            println!("finished client");
            r
        }
        (_, _) => unimplemented!(),
    }
}

fn main() -> Result<(), Error> {
    hoist::run(async_main())
}

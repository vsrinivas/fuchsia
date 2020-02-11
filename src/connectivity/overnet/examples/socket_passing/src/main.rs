// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    fidl_fuchsia_overnet_socketpassingexample as socketpassing,
    futures::{future::try_join, prelude::*},
};

#[derive(FromArgs)]
/// Echo example for Overnet.
struct TestArgs {
    #[argh(subcommand)]
    subcommand: Subcommand,
}

#[derive(FromArgs, Clone)]
#[argh(subcommand, name = "server")]
/// Run as a server
struct ServerCommand {
    #[argh(option)]
    /// text string to send server->client
    send: Option<String>,
    #[argh(option)]
    /// text string to expect client->server
    expect: Option<String>,
}

#[derive(FromArgs)]
#[argh(subcommand, name = "client")]
/// Run as a server
struct ClientCommand {
    #[argh(option)]
    /// text string to send client->server
    send: Option<String>,
    #[argh(option)]
    /// text string to expect server->client
    expect: Option<String>,
}

#[derive(Clone)]
struct Command {
    send: Option<String>,
    expect: Option<String>,
}

impl From<ServerCommand> for Command {
    fn from(c: ServerCommand) -> Command {
        Command { send: c.send, expect: c.expect }
    }
}

impl From<ClientCommand> for Command {
    fn from(c: ClientCommand) -> Command {
        Command { send: c.send, expect: c.expect }
    }
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum Subcommand {
    Server(ServerCommand),
    Client(ClientCommand),
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(args: Command) -> Result<(), Error> {
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
            return try_join(run_command(cs, args), cli.pass(ss).map_err(|e| e.into()))
                .await
                .map(|_| ());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

async fn run_command(socket: fidl::Socket, args: Command) -> Result<(), Error> {
    let (mut rx, mut tx) = fidl::AsyncSocket::from_socket(socket)?.split();

    let send = args.send;
    let expect = args.expect;
    try_join(
        async move {
            if let Some(send) = send {
                println!("SEND: {}", send);
                let buf: Vec<u8> = send.bytes().collect();
                let mut ofs = 0;
                while ofs != buf.len() {
                    ofs += tx.write(&buf[ofs..]).await?;
                }
            }
            Ok(()) as Result<(), Error>
        },
        async move {
            if let Some(expect) = expect {
                println!("EXPECT: {}", expect);
                let mut buf = [0u8; 1024];
                let mut ofs = 0;
                while ofs < expect.len() {
                    let n = rx.read(&mut buf[ofs..]).await?;
                    println!("got bytes: {:?}", &buf[ofs..ofs + n]);
                    ofs += n;
                }
                assert_eq!(ofs, expect.len());
                assert_eq!(&buf[..ofs], expect.bytes().collect::<Vec<u8>>().as_slice());
            }
            Ok(())
        },
    )
    .await?;
    Ok(())
}

fn spawn_example_server(chan: fidl::AsyncChannel, args: Command) {
    hoist::spawn(
        async move {
            let mut stream = socketpassing::ExampleRequestStream::from_channel(chan);
            while let Some(request) =
                stream.try_next().await.context("error running echo server")?
            {
                match request {
                    socketpassing::ExampleRequest::Pass { socket, responder } => {
                        println!("Received socket request");
                        let args = args.clone();
                        run_command(socket, args).await?;
                        responder.send()?;
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

async fn exec_server(args: Command) -> Result<(), Error> {
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
        println!("Received service request for service");
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        spawn_example_server(chan, args.clone());
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    std::env::set_var("RUST_BACKTRACE", "full");

    match argh::from_env::<TestArgs>().subcommand {
        Subcommand::Server(server_args) => exec_server(server_args.into()).await,
        Subcommand::Client(client_args) => exec_client(client_args.into()).await,
    }
}

fn main() -> Result<(), Error> {
    hoist::run(async_main())
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    clap::{App, Arg, SubCommand},
    fidl::endpoints::{ClientEnd, RequestStream, ServerEnd, ServiceMarker},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    fidl_fuchsia_overnet_examples_interfacepassing as interfacepassing,
    fidl_test_placeholders as echo,
    futures::prelude::*,
};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("overnet-interface-passing")
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

async fn exec_client(text: Option<&str>) -> Result<(), Error> {
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
                .find(|name| *name == interfacepassing::ExampleMarker::NAME)
                .is_none()
            {
                continue;
            }
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            if let Err(e) =
                svc.connect_to_service(&mut peer.id, interfacepassing::ExampleMarker::NAME, s)
            {
                println!("{:?}", e);
                continue;
            }
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = interfacepassing::ExampleProxy::new(proxy);

            let (s1, p1) = fidl::Channel::create().context("failed to create zx channel")?;
            let proxy_echo =
                fidl::AsyncChannel::from_channel(p1).context("failed to make async channel")?;
            let cli_echo = echo::EchoProxy::new(proxy_echo);
            println!("Sending {:?} to {:?}", text, peer.id);
            if let Err(e) = cli.request(ServerEnd::new(s1)) {
                println!("ERROR REQUESTING INTERFACE: {:?}", e);
                continue;
            }
            println!("received {:?}", cli_echo.echo_string(text).await?);
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

async fn echo_server(chan: ServerEnd<echo::EchoMarker>, quiet: bool) -> Result<(), Error> {
    chan.into_stream()?
        .map_err(Into::into)
        .try_for_each_concurrent(
            None,
            |echo::EchoRequest::EchoString { value, responder }| async move {
                if !quiet {
                    println!("Received echo request for string {:?}", value);
                }
                responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
                if !quiet {
                    println!("echo response sent successfully");
                }
                Ok(())
            },
        )
        .await
}

async fn example_server(chan: fidl::AsyncChannel, quiet: bool) -> Result<(), Error> {
    interfacepassing::ExampleRequestStream::from_channel(chan)
        .map_err(Into::into)
        .try_for_each_concurrent(
            None,
            |interfacepassing::ExampleRequest::Request { iface, .. }| {
                if !quiet {
                    println!("Received interface request");
                }
                echo_server(iface, quiet)
            },
        )
        .await
}

async fn exec_server(quiet: bool) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    hoist::publish_service(interfacepassing::ExampleMarker::NAME, ClientEnd::new(p))?;
    ServiceProviderRequestStream::from_channel(chan)
        .map_err(Into::into)
        .try_for_each_concurrent(
            None,
            |ServiceProviderRequest::ConnectToService {
                 chan,
                 info: _,
                 control_handle: _control_handle,
             }| {
                async move {
                    if !quiet {
                        println!("Received service request for service");
                    }
                    let chan = fidl::AsyncChannel::from_channel(chan)
                        .context("failed to make async channel")?;
                    example_server(chan, quiet).await
                }
            },
        )
        .await
}

////////////////////////////////////////////////////////////////////////////////
// main

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args = app().get_matches();

    match args.subcommand() {
        ("server", Some(_)) => exec_server(args.is_present("quiet")).await,
        ("client", Some(cmd)) => {
            let r = exec_client(cmd.value_of("text")).await;
            println!("finished client");
            r
        }
        (_, _) => unimplemented!(),
    }
}

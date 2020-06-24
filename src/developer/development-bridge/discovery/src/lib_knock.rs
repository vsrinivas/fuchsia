// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    ffx_core::ffx_plugin,
    ffx_knock_args::KnockCommand,
    fidl::handle::Channel,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fuchsia_zircon_status as zx_status, selectors,
    std::io::{stdout, Write},
};

pub const SELECTOR_FORMAT_HELP: &str = "Selector format: <component moniker>:(in|out|exposed)[:<service name>]. Wildcards may be used anywhere in the selector.
Example: 'remote-control:out:*' would return all services in 'out' for the component remote-control.

Note that moniker wildcards are not recursive: 'a/*/c' will only match components named 'c' running in some sub-realm directly below 'a', and no further.";

#[ffx_plugin()]
pub async fn knock_cmd(remote_proxy: RemoteControlProxy, cmd: KnockCommand) -> Result<(), Error> {
    let writer = Box::new(stdout());
    knock(remote_proxy, writer, &cmd.selector).await
}

async fn knock<W: Write>(
    remote_proxy: RemoteControlProxy,
    mut write: W,
    selector: &str,
) -> Result<(), Error> {
    let writer = &mut write;
    let selector = match selectors::parse_selector(selector) {
        Ok(s) => s,
        Err(e) => {
            writeln!(writer, "Failed to parse the provided selector: {:?}", e)?;
            writeln!(writer, "{}", SELECTOR_FORMAT_HELP)?;
            return Ok(());
        }
    };

    let (client, server) = Channel::create()?;
    match remote_proxy.connect(selector, server).await.context("awaiting connect call")? {
        Ok(m) => {
            match client.read_split(&mut vec![], &mut vec![]) {
                Err(zx_status::Status::PEER_CLOSED) => writeln!(
                    writer,
                    "Failure: service is not up. Connection to '{}:{}:{}' reported PEER_CLOSED.",
                    m.moniker.join("/"),
                    m.subdir,
                    m.service
                )?,
                Err(zx_status::Status::SHOULD_WAIT) => writeln!(
                    writer,
                    "Success: service is up. Connected to '{}:{}:{}'.",
                    m.moniker.join("/"),
                    m.subdir,
                    m.service
                )?,
                Err(e) => writeln!(
                    writer,
                    "Unknown: opened connection to '{}:{}:{}', but channel read reported {:?}.",
                    m.moniker.join("/"),
                    m.subdir,
                    m.service,
                    e
                )?,
                _ => writeln!(
                    writer,
                    "Success: service is up. Connected to '{}:{}:{}'.",
                    m.moniker.join("/"),
                    m.subdir,
                    m.service
                )?,
            };

            Ok(())
        }
        Err(e) => {
            writeln!(writer, "Failed to connect to service: {:?}", e)?;
            Ok(())
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl::handle::AsyncChannel,
        fidl_fuchsia_developer_bridge::{DaemonRequest, DaemonRequestStream},
        fidl_fuchsia_developer_remotecontrol::{
            RemoteControlMarker, RemoteControlRequest, ServiceMatch,
        },
        futures::TryStreamExt,
        std::io::BufWriter,
    };

    fn setup_fake_daemon_service(mut stream: DaemonRequestStream) {
        hoist::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    DaemonRequest::EchoString { value, responder } => {
                        responder.send(&value).unwrap();
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        });
    }

    fn setup_fake_remote_server(connect_chan: bool) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
        hoist::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RemoteControlRequest::Connect { selector: _, service_chan, responder } => {
                        if connect_chan {
                            setup_fake_daemon_service(DaemonRequestStream::from_channel(
                                AsyncChannel::from_channel(service_chan).unwrap(),
                            ));
                        }

                        let _ = responder
                            .send(&mut Ok(ServiceMatch {
                                moniker: vec![String::from("core"), String::from("test")],
                                subdir: String::from("out"),
                                service: String::from("fuchsia.myservice"),
                            }))
                            .unwrap();
                    }
                    _ => assert!(false, format!("got unexpected {:?}", req)),
                }
            }
        });

        proxy
    }

    #[test]
    fn test_knock_invalid_selector() -> Result<(), Error> {
        let mut output = String::new();
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let remote_proxy = setup_fake_remote_server(false);
            let _response =
                knock(remote_proxy, writer, "a:b:").await.expect("knock should not fail");
            assert!(output.contains(SELECTOR_FORMAT_HELP));
        });
        Ok(())
    }

    #[test]
    fn test_knock_working_service() -> Result<(), Error> {
        let mut output = String::new();
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let remote_proxy = setup_fake_remote_server(true);
            let _response =
                knock(remote_proxy, writer, "*:*:*").await.expect("knock should not fail");
            assert!(output.contains("Success"));
            assert!(output.contains("core/test:out:fuchsia.myservice"));
        });
        Ok(())
    }

    #[test]
    fn test_knock_no_service_connected() -> Result<(), Error> {
        let mut output = String::new();
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let remote_proxy = setup_fake_remote_server(false);
            let _response =
                knock(remote_proxy, writer, "*:*:*").await.expect("knock should not fail");
            assert!(!output.contains("Success"));
            assert!(output.contains("Failure"));
            assert!(output.contains("core/test:out:fuchsia.myservice"));
        });
        Ok(())
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::ffx_error,
    ffx_component::SELECTOR_FORMAT_HELP,
    ffx_core::ffx_plugin,
    ffx_knock_args::{KnockCommand, Node},
    fidl::client::Client,
    fidl::handle::Channel,
    fidl_fuchsia_developer_remotecontrol as rc,
    fuchsia_async::{Duration, TimeoutExt},
    fuchsia_zircon_status as zx_status,
    futures::{StreamExt, TryStreamExt},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    selectors::{self, VerboseError},
    std::io::{stdout, Write},
    thiserror::Error,
};

fn generate_selector(moniker: String, service: String, node: Node) -> Result<String> {
    let mut moniker = AbsoluteMoniker::parse_str(moniker.as_str())
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?
        .to_string();

    // Remove the leading '/' if present.
    if moniker.chars().next().unwrap() == '/' {
        moniker.remove(0);
    }

    Ok([moniker, node.to_string(), service].join(":"))
}

#[ffx_plugin()]
pub async fn knock_cmd(remote_proxy: rc::RemoteControlProxy, cmd: KnockCommand) -> Result<()> {
    let writer = Box::new(stdout());

    knock(
        remote_proxy,
        writer,
        generate_selector(cmd.moniker, cmd.service, cmd.node)?.as_str(),
        cmd.timeout,
    )
    .await
}

async fn knock<W: Write>(
    remote_proxy: rc::RemoteControlProxy,
    mut write: W,
    selector_str: &str,
    timeout_secs: u64,
) -> Result<()> {
    let writer = &mut write;
    let selector = selectors::parse_selector::<VerboseError>(selector_str).map_err(|e| {
        ffx_error!("Invalid selector '{}': {}\n{}", selector_str, e, SELECTOR_FORMAT_HELP)
    })?;

    let (client, server) = Channel::create()?;
    match remote_proxy.connect(selector, server).await.context("awaiting connect call")? {
        Ok(m) => {
            let client = fuchsia_async::Channel::from_channel(client)?;
            let client = Client::new(client, "protocol_name");

            let mut event_receiver = client.take_event_receiver().map_err(|err| match err {
                fidl::Error::ClientRead(status) if status == zx_status::Status::PEER_CLOSED => {
                    KnockError::PeerClosed
                }
                other => KnockError::Fidl { err: other },
            });
            let result = event_receiver
                .next()
                .on_timeout(Duration::from_secs(timeout_secs), || {
                    Some(Err(KnockError::Timeout { duration: timeout_secs }))
                })
                .await;

            match result {
                None => {
                    writeln!(
                        writer,
                        "Failure: service is not up. Connection to '{}:{}:{}' returned none.",
                        m.moniker.join("/"),
                        m.subdir,
                        m.service
                    )?;
                }
                Some(result) => match result {
                    Err(KnockError::Timeout { duration }) => {
                        writeln!(
                            writer,
                            "Connection to '{}:{}:{}' is alive after {} seconds. Assuming success.",
                            m.moniker.join("/"),
                            m.subdir,
                            m.service,
                            duration
                        )?;
                    }
                    Err(KnockError::PeerClosed) => {
                        writeln!(
                            writer,
                            "Failure: service is not up. Connection to '{}:{}:{}' reported PEER_CLOSED.",
                            m.moniker.join("/"),
                            m.subdir,
                            m.service
                        )?;
                    }
                    Err(KnockError::Fidl { err }) => {
                        writeln!(
                            writer,
                            "Unknown: opened connection to '{}:{}:{}', but channel read reported {:?}.",
                            m.moniker.join("/"),
                            m.subdir,
                            m.service,
                            err
                        )?;
                    }
                    Ok(_) => {
                        writeln!(
                            writer,
                            "Success: service is up. Connected to '{}:{}:{}'.",
                            m.moniker.join("/"),
                            m.subdir,
                            m.service
                        )?;
                    }
                },
            }

            Ok(())
        }
        Err(e) => {
            writeln!(writer, "Failed to connect to service `{}`: {:?}", selector_str, e)?;
            Ok(())
        }
    }
}

/// The error type used by Knock operations.
#[derive(Debug, Error, Clone)]
pub enum KnockError {
    /// The timeout has been reached while knocking
    #[error("Timeout reached. No response received after {} seconds", duration)]
    Timeout { duration: u64 },

    /// Got a Fidl client read error with status PEER_CLOSED
    #[error("FIDL client read error with status PEER_CLOSED")]
    PeerClosed,

    /// A FIDL error has been thrown while knocking.
    #[error("FIDL error: {}", err)]
    Fidl { err: fidl::Error },
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*, anyhow::Result, fidl::endpoints::RequestStream, fidl::handle::AsyncChannel,
        fidl_fuchsia_developer_ffx::DaemonRequestStream, futures::TryStreamExt,
    };

    fn setup_fake_daemon_service(mut stream: DaemonRequestStream) {
        fuchsia_async::Task::local(async move {
            let mut continue_once = true;
            while let Ok(Some(_req)) = stream.try_next().await {
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                if continue_once {
                    continue_once = false;
                    continue;
                }
                break;
            }
        })
        .detach();
    }

    fn setup_fake_remote_server(connect_chan: bool) -> rc::RemoteControlProxy {
        setup_fake_remote_proxy(move |req| match req {
            rc::RemoteControlRequest::Connect { selector: _, service_chan, responder } => {
                if connect_chan {
                    setup_fake_daemon_service(DaemonRequestStream::from_channel(
                        AsyncChannel::from_channel(service_chan).unwrap(),
                    ));
                }

                let _ = responder
                    .send(&mut Ok(rc::ServiceMatch {
                        moniker: vec![String::from("core"), String::from("test")],
                        subdir: String::from("out"),
                        service: String::from("fuchsia.myservice"),
                    }))
                    .unwrap();
            }
            _ => assert!(false, "got unexpected {:?}", req),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_knock_invalid_selector() -> Result<()> {
        let mut output = Vec::new();
        let remote_proxy = setup_fake_remote_server(false);
        let response = knock(remote_proxy, &mut output, "a:b:", 5).await;
        let e = response.unwrap_err();
        assert!(e.to_string().contains(SELECTOR_FORMAT_HELP));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_knock_working_service() -> Result<()> {
        let mut output_utf8 = Vec::new();
        let remote_proxy = setup_fake_remote_server(true);
        let _response =
            knock(remote_proxy, &mut output_utf8, "*:*:*", 5).await.expect("knock should not fail");

        let output = String::from_utf8(output_utf8).expect("Invalid UTF-8 bytes");
        assert!(output
            .contains("Connection to 'core/test:out:fuchsia.myservice' is alive after 5 seconds. Assuming success."));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_knock_no_service_connected() -> Result<()> {
        let mut output_utf8 = Vec::new();
        let remote_proxy = setup_fake_remote_server(false);
        let _response =
            knock(remote_proxy, &mut output_utf8, "*:*:*", 5).await.expect("knock should not fail");

        let output = String::from_utf8(output_utf8).expect("Invalid UTF-8 bytes");
        assert!(!output.contains("Success"));
        assert!(output.contains("Failure"));
        assert!(output.contains("core/test:out:fuchsia.myservice"));
        Ok(())
    }

    #[test]
    fn test_generate_selector() {
        assert_eq!(
            generate_selector(
                "/core/cobalt".to_string(),
                "fuchsia.net.http.Loader".to_string(),
                Node::In
            )
            .unwrap(),
            "core/cobalt:in:fuchsia.net.http.Loader"
        );
        assert_eq!(
            generate_selector(
                "INVALID_MONIKER".to_string(),
                "fuchsia.net.http.Loader".to_string(),
                Node::In
            )
            .unwrap_err()
            .to_string(),
            "Moniker could not be parsed: invalid moniker: INVALID_MONIKER".to_string()
        );
    }
}

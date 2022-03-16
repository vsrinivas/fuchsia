// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, format_err, Context as _, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_wlan_common as wlan_common,
    fidl_fuchsia_wlan_policy::{self as wlan_policy, NetworkConfig},
    fuchsia_async::{Time, TimeoutExt as _},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::Duration,
    futures::TryStreamExt as _,
};

const CONNECT_TIMEOUT: Duration = Duration::from_seconds(60);

type GetClientController = dyn Fn() -> Result<
    (wlan_policy::ClientControllerProxy, wlan_policy::ClientStateUpdatesRequestStream),
    Error,
>;

pub fn create_network_info(
    ssid: &str,
    pass: Option<&str>,
    security_type: Option<wlan_policy::SecurityType>,
) -> NetworkConfig {
    let credential = pass.map_or(wlan_policy::Credential::None(wlan_policy::Empty), |pass| {
        wlan_policy::Credential::Password(pass.as_bytes().to_vec())
    });
    let network_id = wlan_policy::NetworkIdentifier {
        ssid: ssid.as_bytes().to_vec(),
        type_: security_type.unwrap_or(wlan_policy::SecurityType::None),
    };
    wlan_policy::NetworkConfig {
        id: Some(network_id),
        credential: Some(credential),
        ..wlan_policy::NetworkConfig::EMPTY
    }
}

fn get_client_controller(
) -> Result<(wlan_policy::ClientControllerProxy, wlan_policy::ClientStateUpdatesRequestStream), Error>
{
    let policy_provider = connect_to_protocol::<wlan_policy::ClientProviderMarker>()?;
    let (client_controller, server_end) = create_proxy::<wlan_policy::ClientControllerMarker>()
        .context("create ClientController proxy")?;
    let (update_client_end, update_stream) =
        create_request_stream::<wlan_policy::ClientStateUpdatesMarker>()
            .context("create ClientStateUpdates request stream")?;
    policy_provider
        .get_controller(server_end, update_client_end)
        .context("PolicyProvider.get_controller")?;

    Ok((client_controller, update_stream))
}

#[async_trait(?Send)]
pub trait WifiConnect {
    async fn connect(&self, network: NetworkConfig) -> Result<(), Error>;
}

pub struct WifiConnectImpl {
    get_client_controller: Box<GetClientController>,
}

impl WifiConnectImpl {
    pub fn new() -> Self {
        Self { get_client_controller: Box::new(get_client_controller) }
    }

    async fn wait_for_connection(
        &self,
        mut client_state_updates_request: wlan_policy::ClientStateUpdatesRequestStream,
    ) -> Result<(), Error> {
        while let Some(update_request) = client_state_updates_request.try_next().await? {
            let update = update_request.into_on_client_state_update();
            let (update, responder) = match update {
                Some((update, responder)) => (update, responder),
                None => return Err(format_err!("Client provider produced invalid update.")),
            };
            let _ = responder.send();
            if let Some(networks) = update.networks {
                if networks
                    .iter()
                    .any(|ns| ns.state == Some(wlan_policy::ConnectionState::Connected))
                {
                    // Connected to a WiFi network
                    break;
                }
            }
        }

        Ok(())
    }
}

#[async_trait(?Send)]
impl WifiConnect for WifiConnectImpl {
    async fn connect(&self, network_config: NetworkConfig) -> Result<(), Error> {
        let (client_controller, client_state_updates_request) = (self.get_client_controller)()?;

        let mut network_id = network_config.id.clone().unwrap();

        match client_controller.save_network(network_config).await? {
            Ok(()) => {
                let result = client_controller.connect(&mut network_id).await?;
                if result == wlan_common::RequestStatus::Acknowledged {
                    Ok(())
                } else {
                    Err(format_err!("Unexpected return from connect: {:?}", result))
                }
            }
            Err(e) => Err(format_err!("failed to save network with {:?}", e)),
        }?;
        self.wait_for_connection(client_state_updates_request)
            .on_timeout(Time::after(CONNECT_TIMEOUT), || {
                bail!("Timed out waiting for wlan connection")
            })
            .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[allow(unused_imports)]
    use {
        crate::testing::*, anyhow::bail, fuchsia_async as fasync, futures::future::FutureExt,
        matches::assert_matches, pin_utils::pin_mut, std::cell::Cell, std::future::Future,
        std::rc::Rc,
    };

    fn mock_wlan_policy<H, F>(handler: H) -> Result<Box<GetClientController>, Error>
    where
        F: 'static + Future<Output = ()>,
        H: Fn(
            wlan_policy::ClientControllerRequestStream,
            wlan_policy::ClientStateUpdatesProxy,
        ) -> F,
    {
        // Create FIDL endpoints for ClientController and ClientStateUpdates protocols.
        let (client_controller_proxy, client_controller_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<wlan_policy::ClientControllerMarker>()?;
        let (client_state_updates_proxy, client_state_updates_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<wlan_policy::ClientStateUpdatesMarker>()?;

        // Spawn handler.
        fasync::Task::local(handler(client_controller_request_stream, client_state_updates_proxy))
            .detach();

        // Make a function that will return the ClientControllerProxy and
        // ClientStateUpdatesRequestStream exactly once.
        let gcc_return =
            Cell::new(Some((client_controller_proxy, client_state_updates_request_stream)));
        let gcc = Box::new(move || Ok(gcc_return.replace(None).unwrap()));

        Ok(gcc)
    }

    #[fasync::run_until_stalled(test)]
    async fn connect() {
        fn network_id() -> wlan_policy::NetworkIdentifier {
            wlan_policy::NetworkIdentifier {
                ssid: vec![64, 64, 64, 64],
                type_: wlan_policy::SecurityType::Wpa2,
            }
        }
        fn network_config() -> wlan_policy::NetworkConfig {
            wlan_policy::NetworkConfig {
                id: Some(network_id()),
                credential: Some(wlan_policy::Credential::Password(vec![66, 66, 66, 66])),
                ..wlan_policy::NetworkConfig::EMPTY
            }
        }

        let get_client_controller = mock_wlan_policy(|mut request_stream, proxy| async move {
            use wlan_policy::ClientControllerRequest::*;

            while let Some(request) = request_stream.try_next().await.unwrap() {
                match request {
                    SaveNetwork { config, responder } => {
                        assert_eq!(config, network_config());
                        responder.send(&mut Ok(())).unwrap();
                    }
                    Connect { id, responder } => {
                        assert_eq!(id, network_id());
                        responder.send(wlan_common::RequestStatus::Acknowledged).unwrap();

                        proxy
                            .on_client_state_update(wlan_policy::ClientStateSummary {
                                state: Some(wlan_policy::WlanClientState::ConnectionsEnabled),
                                networks: Some(vec![wlan_policy::NetworkState {
                                    id: Some(network_id()),
                                    state: Some(wlan_policy::ConnectionState::Connecting),
                                    status: None,
                                    ..wlan_policy::NetworkState::EMPTY
                                }]),
                                ..wlan_policy::ClientStateSummary::EMPTY
                            })
                            .await
                            .expect("sending client state update");
                        proxy
                            .on_client_state_update(wlan_policy::ClientStateSummary {
                                state: Some(wlan_policy::WlanClientState::ConnectionsEnabled),
                                networks: Some(vec![wlan_policy::NetworkState {
                                    id: Some(network_id()),
                                    state: Some(wlan_policy::ConnectionState::Connected),
                                    status: None,
                                    ..wlan_policy::NetworkState::EMPTY
                                }]),
                                ..wlan_policy::ClientStateSummary::EMPTY
                            })
                            .await
                            .expect("sending client state update");
                    }
                    _ => unimplemented!(),
                }
            }
        })
        .expect("create mock");
        let wci = WifiConnectImpl { get_client_controller };
        wci.connect(network_config()).await.expect("connect");
    }

    #[test]
    fn connect_timeout() {
        // Fake time to test the timeout.
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(0));

        fn network_id() -> wlan_policy::NetworkIdentifier {
            wlan_policy::NetworkIdentifier {
                ssid: vec![64, 64, 64, 64],
                type_: wlan_policy::SecurityType::Wpa2,
            }
        }
        fn network_config() -> wlan_policy::NetworkConfig {
            wlan_policy::NetworkConfig {
                id: Some(network_id()),
                credential: Some(wlan_policy::Credential::Password(vec![66, 66, 66, 66])),
                ..wlan_policy::NetworkConfig::EMPTY
            }
        }

        let get_client_controller = mock_wlan_policy(|mut request_stream, proxy| async move {
            use wlan_policy::ClientControllerRequest::*;

            while let Some(request) = request_stream.try_next().await.unwrap() {
                match request {
                    SaveNetwork { config, responder } => {
                        assert_eq!(config, network_config());
                        responder.send(&mut Ok(())).unwrap();
                    }
                    Connect { id, responder } => {
                        assert_eq!(id, network_id());
                        responder.send(wlan_common::RequestStatus::Acknowledged).unwrap();

                        proxy
                            .on_client_state_update(wlan_policy::ClientStateSummary {
                                state: Some(wlan_policy::WlanClientState::ConnectionsEnabled),
                                networks: Some(vec![wlan_policy::NetworkState {
                                    id: Some(network_id()),
                                    state: Some(wlan_policy::ConnectionState::Connecting),
                                    status: None,
                                    ..wlan_policy::NetworkState::EMPTY
                                }]),
                                ..wlan_policy::ClientStateSummary::EMPTY
                            })
                            .await
                            .expect("sending client state update");
                        proxy
                            .on_client_state_update(wlan_policy::ClientStateSummary {
                                state: Some(wlan_policy::WlanClientState::ConnectionsEnabled),
                                networks: Some(vec![wlan_policy::NetworkState {
                                    id: Some(network_id()),
                                    state: Some(wlan_policy::ConnectionState::Disconnected),
                                    status: None,
                                    ..wlan_policy::NetworkState::EMPTY
                                }]),
                                ..wlan_policy::ClientStateSummary::EMPTY
                            })
                            .await
                            .expect("sending client state update");
                    }
                    _ => unimplemented!(),
                }
            }
        })
        .expect("create mock");

        let wci = WifiConnectImpl { get_client_controller };

        let mut connect_future = Box::pin(wci.connect(network_config()));
        // The future shouldn't complete before sleeping, waiting for a connection
        assert!(exec.run_until_stalled(&mut connect_future).is_pending());
        // .... the passage of time...
        exec.wake_next_timer().unwrap();
        assert_matches!(
            exec.run_until_stalled(&mut connect_future),
            futures::task::Poll::Ready(Err(_))
        );
    }
}

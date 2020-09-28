// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::generic::{CurrentStateCache, Listener, Message},
    fidl_fuchsia_wlan_policy as fidl_policy,
    futures::{channel::mpsc, future::BoxFuture, prelude::*},
};

#[derive(Clone, Debug, PartialEq)]
pub struct ClientNetworkState {
    pub id: fidl_policy::NetworkIdentifier,
    pub state: fidl_policy::ConnectionState,
    pub status: Option<fidl_policy::DisconnectStatus>,
}

impl Into<fidl_policy::NetworkState> for ClientNetworkState {
    fn into(self) -> fidl_policy::NetworkState {
        fidl_policy::NetworkState {
            id: Some(self.id),
            state: Some(self.state),
            status: self.status,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ClientStateUpdate {
    pub state: Option<fidl_policy::WlanClientState>,
    pub networks: Vec<ClientNetworkState>,
}

impl Into<fidl_policy::ClientStateSummary> for ClientStateUpdate {
    fn into(self) -> fidl_policy::ClientStateSummary {
        fidl_policy::ClientStateSummary {
            state: self.state,
            networks: Some(self.networks.iter().map(|n| n.clone().into()).collect()),
        }
    }
}

impl CurrentStateCache for ClientStateUpdate {
    fn default() -> ClientStateUpdate {
        ClientStateUpdate { state: None, networks: vec![] }
    }

    fn merge_in_update(&mut self, update: Self) {
        self.state = update.state.or(self.state);
        // Keep a cache of "active" networks
        self.networks = self
            .networks
            .iter()
            // Only keep "active" networks (i.e. connecting/connected)
            .filter(|n| match n.state {
                fidl_policy::ConnectionState::Failed => false,
                fidl_policy::ConnectionState::Disconnected => false,
                fidl_policy::ConnectionState::Connecting => true,
                fidl_policy::ConnectionState::Connected => true,
            })
            // Remove networks present in the update, we'll use that data directly below
            .filter(|n| update.networks.iter().find(|u| u.id == n.id).is_none())
            .map(|n| n.clone())
            .collect();
        // Push in all the networks from the update
        for network in update.networks.iter() {
            self.networks.push(network.clone());
        }
    }
}
impl Listener<fidl_policy::ClientStateSummary> for fidl_policy::ClientStateUpdatesProxy {
    fn notify_listener(
        self,
        update: fidl_policy::ClientStateSummary,
    ) -> BoxFuture<'static, Option<Box<Self>>> {
        let fut =
            async move { self.on_client_state_update(update).await.ok().map(|()| Box::new(self)) };
        fut.boxed()
    }
}

// Helpful aliases for servicing client updates
pub type ClientListenerMessage = Message<fidl_policy::ClientStateUpdatesProxy, ClientStateUpdate>;
pub type ClientListenerMessageSender = mpsc::UnboundedSender<ClientListenerMessage>;

#[cfg(test)]
mod tests {
    use {
        super::{super::generic::CurrentStateCache, *},
        fidl_fuchsia_wlan_policy as fidl_policy,
    };

    #[test]
    fn merge_update_none_to_one_active() {
        let mut current_state_cache = ClientStateUpdate::default();
        assert_eq!(current_state_cache, ClientStateUpdate { state: None, networks: vec![] });

        // Merge an update with one connected network.
        let update = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from("ssid 1").into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        current_state_cache.merge_in_update(update);

        assert_eq!(
            current_state_cache,
            ClientStateUpdate {
                state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
                networks: vec![ClientNetworkState {
                    id: fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 1").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    },
                    state: fidl_policy::ConnectionState::Connected,
                    status: None,
                }],
            }
        );
    }

    #[test]
    fn merge_update_one_to_two_active() {
        // Start with a connected network
        let mut current_state_cache = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from("ssid 1").into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };

        // Merge an update with a different connecting network.
        let update = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from("ssid 2").into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        current_state_cache.merge_in_update(update);

        // Both networks are "active" and should be present
        assert_eq!(
            current_state_cache,
            ClientStateUpdate {
                state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
                networks: vec![
                    ClientNetworkState {
                        id: fidl_policy::NetworkIdentifier {
                            ssid: String::from("ssid 1").into_bytes(),
                            type_: fidl_policy::SecurityType::Wpa2,
                        },
                        state: fidl_policy::ConnectionState::Connected,
                        status: None,
                    },
                    ClientNetworkState {
                        id: fidl_policy::NetworkIdentifier {
                            ssid: String::from("ssid 2").into_bytes(),
                            type_: fidl_policy::SecurityType::Wpa2,
                        },
                        state: fidl_policy::ConnectionState::Connecting,
                        status: None,
                    }
                ],
            }
        );
    }

    #[test]
    fn merge_update_two_to_one_active() {
        // Start with two active networks
        let mut current_state_cache = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![
                ClientNetworkState {
                    id: fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 1").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    },
                    state: fidl_policy::ConnectionState::Connected,
                    status: None,
                },
                ClientNetworkState {
                    id: fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 2").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    },
                    state: fidl_policy::ConnectionState::Connecting,
                    status: None,
                },
            ],
        };

        // Merge an update with one network becoming inactive.
        let update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from("ssid 1").into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
            }],
        };
        current_state_cache.merge_in_update(update);

        // The other active network should still be present, and we should see
        // the first network becoming disconnected
        assert_eq!(
            current_state_cache,
            ClientStateUpdate {
                state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
                networks: vec![
                    ClientNetworkState {
                        id: fidl_policy::NetworkIdentifier {
                            ssid: String::from("ssid 2").into_bytes(),
                            type_: fidl_policy::SecurityType::Wpa2,
                        },
                        state: fidl_policy::ConnectionState::Connecting,
                        status: None,
                    },
                    ClientNetworkState {
                        id: fidl_policy::NetworkIdentifier {
                            ssid: String::from("ssid 1").into_bytes(),
                            type_: fidl_policy::SecurityType::Wpa2,
                        },
                        state: fidl_policy::ConnectionState::Disconnected,
                        status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
                    },
                ],
            }
        );

        // Send another update about the second still-active network
        let update = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from("ssid 2").into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        current_state_cache.merge_in_update(update);

        // The disconnected network should have dropped off
        assert_eq!(
            current_state_cache,
            ClientStateUpdate {
                state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
                networks: vec![ClientNetworkState {
                    id: fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 2").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    },
                    state: fidl_policy::ConnectionState::Connected,
                    status: None,
                },],
            }
        );
    }

    #[test]
    fn into_fidl() {
        let single_network_state = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from("ssid 1").into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        let fidl_state: fidl_policy::ClientStateSummary = single_network_state.into();
        assert_eq!(
            fidl_state,
            fidl_policy::ClientStateSummary {
                state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
                networks: Some(vec![fidl_policy::NetworkState {
                    id: Some(fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 1").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    }),
                    state: Some(fidl_policy::ConnectionState::Connected),
                    status: None,
                }]),
            }
        );

        let multi_network_state = ClientStateUpdate {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![
                ClientNetworkState {
                    id: fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 2").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    },
                    state: fidl_policy::ConnectionState::Connecting,
                    status: None,
                },
                ClientNetworkState {
                    id: fidl_policy::NetworkIdentifier {
                        ssid: String::from("ssid 1").into_bytes(),
                        type_: fidl_policy::SecurityType::Wpa2,
                    },
                    state: fidl_policy::ConnectionState::Disconnected,
                    status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
                },
            ],
        };
        let fidl_state: fidl_policy::ClientStateSummary = multi_network_state.into();
        assert_eq!(
            fidl_state,
            fidl_policy::ClientStateSummary {
                state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
                networks: Some(vec![
                    fidl_policy::NetworkState {
                        id: Some(fidl_policy::NetworkIdentifier {
                            ssid: String::from("ssid 2").into_bytes(),
                            type_: fidl_policy::SecurityType::Wpa2,
                        }),
                        state: Some(fidl_policy::ConnectionState::Connecting),
                        status: None,
                    },
                    fidl_policy::NetworkState {
                        id: Some(fidl_policy::NetworkIdentifier {
                            ssid: String::from("ssid 1").into_bytes(),
                            type_: fidl_policy::SecurityType::Wpa2,
                        }),
                        state: Some(fidl_policy::ConnectionState::Disconnected),
                        status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
                    },
                ]),
            }
        );
    }
}

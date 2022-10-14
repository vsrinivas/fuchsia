// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::generic::{CurrentStateCache, Listener, Message},
    crate::access_point::types,
    fidl_fuchsia_wlan_policy as fidl_policy,
    futures::{channel::mpsc, future::BoxFuture, prelude::*},
};

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct ConnectedClientInformation {
    pub count: u8,
}

impl From<ConnectedClientInformation> for fidl_policy::ConnectedClientInformation {
    fn from(connected_client_info: ConnectedClientInformation) -> Self {
        fidl_policy::ConnectedClientInformation {
            count: Some(connected_client_info.count),
            ..fidl_policy::ConnectedClientInformation::EMPTY
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ApStatesUpdate {
    pub access_points: Vec<ApStateUpdate>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ApStateUpdate {
    pub id: types::NetworkIdentifier,
    pub state: types::OperatingState,
    pub mode: Option<types::ConnectivityMode>,
    pub band: Option<types::OperatingBand>,
    pub frequency: Option<u32>,
    pub clients: Option<ConnectedClientInformation>,
}

impl ApStateUpdate {
    pub fn new(
        id: types::NetworkIdentifier,
        state: types::OperatingState,
        mode: types::ConnectivityMode,
        band: types::OperatingBand,
    ) -> Self {
        ApStateUpdate {
            id,
            state,
            mode: Some(mode),
            band: Some(band),
            frequency: None,
            clients: None,
        }
    }
}

impl From<ApStatesUpdate> for Vec<fidl_policy::AccessPointState> {
    fn from(ap_updates: ApStatesUpdate) -> Self {
        ap_updates
            .access_points
            .iter()
            .map(|ap| fidl_policy::AccessPointState {
                id: Some(fidl_policy::NetworkIdentifier::from(ap.id.clone())),
                state: Some(fidl_policy::OperatingState::from(ap.state)),
                mode: ap.mode.map(|mode| fidl_policy::ConnectivityMode::from(mode)),
                band: ap.band.map(|band| fidl_policy::OperatingBand::from(band)),
                frequency: ap.frequency,
                clients: ap.clients.map(|c| c.into()),
                ..fidl_policy::AccessPointState::EMPTY
            })
            .collect()
    }
}

impl CurrentStateCache for ApStatesUpdate {
    fn default() -> ApStatesUpdate {
        ApStatesUpdate { access_points: vec![] }
    }

    fn merge_in_update(&mut self, update: Self) {
        self.access_points = update.access_points;
    }

    fn purge(&mut self) {
        return;
    }
}

impl Listener<Vec<fidl_policy::AccessPointState>> for fidl_policy::AccessPointStateUpdatesProxy {
    fn notify_listener(
        self,
        update: Vec<fidl_policy::AccessPointState>,
    ) -> BoxFuture<'static, Option<Box<Self>>> {
        let fut = async move {
            let mut iter = update.into_iter();
            let fut = self.on_access_point_state_update(&mut iter);
            fut.await.ok().map(|()| Box::new(self))
        };
        fut.boxed()
    }
}

// Helpful aliases for servicing client updates
pub type ApMessage = Message<fidl_policy::AccessPointStateUpdatesProxy, ApStatesUpdate>;
pub type ApListenerMessageSender = mpsc::UnboundedSender<ApMessage>;

#[cfg(test)]
mod tests {
    use {
        super::{super::generic::CurrentStateCache, *},
        crate::client::types::Ssid,
        fidl_fuchsia_wlan_policy as fidl_policy,
        std::convert::TryFrom,
    };

    fn create_network_id() -> types::NetworkIdentifier {
        types::NetworkIdentifier {
            ssid: Ssid::try_from("test").unwrap(),
            security_type: types::SecurityType::None,
        }
    }

    #[fuchsia::test]
    fn merge_updates() {
        let mut current_state_cache = ApStatesUpdate::default();
        assert_eq!(current_state_cache, ApStatesUpdate { access_points: vec![] });

        // Merge in an update with one connected network.
        let update = ApStatesUpdate {
            access_points: vec![{
                ApStateUpdate {
                    id: create_network_id(),
                    state: types::OperatingState::Starting,
                    mode: Some(types::ConnectivityMode::Unrestricted),
                    band: Some(types::OperatingBand::Any),
                    frequency: None,
                    clients: Some(ConnectedClientInformation { count: 0 }),
                }
            }],
        };
        current_state_cache.merge_in_update(update);

        assert_eq!(
            current_state_cache,
            ApStatesUpdate {
                access_points: vec![{
                    ApStateUpdate {
                        id: create_network_id(),
                        state: types::OperatingState::Starting,
                        mode: Some(types::ConnectivityMode::Unrestricted),
                        band: Some(types::OperatingBand::Any),
                        frequency: None,
                        clients: Some(ConnectedClientInformation { count: 0 }),
                    }
                }],
            }
        );
    }

    #[fuchsia::test]
    fn into_fidl() {
        let state = ApStatesUpdate {
            access_points: vec![{
                ApStateUpdate {
                    id: create_network_id(),
                    state: types::OperatingState::Starting,
                    mode: Some(types::ConnectivityMode::Unrestricted),
                    band: Some(types::OperatingBand::Any),
                    frequency: Some(200),
                    clients: Some(ConnectedClientInformation { count: 1 }),
                }
            }],
        };
        let fidl_state: Vec<fidl_policy::AccessPointState> = state.into();
        assert_eq!(
            fidl_state,
            vec![fidl_policy::AccessPointState {
                id: Some(fidl_policy::NetworkIdentifier {
                    ssid: Ssid::try_from("test").unwrap().to_vec(),
                    type_: fidl_policy::SecurityType::None,
                }),
                state: Some(fidl_policy::OperatingState::Starting),
                mode: Some(fidl_policy::ConnectivityMode::Unrestricted),
                band: Some(fidl_policy::OperatingBand::Any),
                frequency: Some(200),
                clients: Some(fidl_policy::ConnectedClientInformation {
                    count: Some(1),
                    ..fidl_policy::ConnectedClientInformation::EMPTY
                }),
                ..fidl_policy::AccessPointState::EMPTY
            }]
        );
    }
}

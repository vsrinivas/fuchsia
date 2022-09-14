// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl::endpoints::create_proxy;
use fidl_fuchsia_net_interfaces::*;
use fuchsia_component::client::connect_to_protocol;
use std::collections::HashSet;
use {crate::prelude_internal::*, anyhow::Error, futures::stream::BoxStream};

#[derive(thiserror::Error, Debug, Eq, PartialEq)]
pub struct BackboneNetworkChanged;

impl std::fmt::Display for BackboneNetworkChanged {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

#[derive(Debug)]
pub struct BackboneNetworkInterface {
    id: u64,
}

#[async_trait::async_trait]
pub trait BackboneInterface: Send + Sync {
    fn get_nicid(&self) -> u64;
    fn event_stream(&self) -> BoxStream<'_, Result<bool, Error>>;
    async fn is_backbone_if_running(&self) -> bool;
}

impl BackboneNetworkInterface {
    pub fn new(nicid: u64) -> BackboneNetworkInterface {
        BackboneNetworkInterface { id: nicid }
    }

    fn watch_for_new_backbone_if_boxed_stream(&self) -> BoxStream<'_, Result<bool, Error>> {
        // We won't return any Ok(). Will just check and return Err() when a valid interface with
        // default route in wlan is added.

        // for each Event::Existing and Event::Added, check the default route on that interface.
        // The restart should be done instantly without additional delay
        let watch_all_if_events_stream = futures::stream::try_unfold(
            None,
            move |_state: Option<()>| {
                async move {
                    let fnif_state = connect_to_protocol::<StateMarker>()?;
                    let (watcher_client, req) = create_proxy::<WatcherMarker>()?;
                    fnif_state.get_watcher(WatcherOptions::EMPTY, req)?;
                    let watcher = Some(watcher_client);
                    let mut wlan_nicid_set = HashSet::new();
                    loop {
                        match watcher.as_ref().unwrap().watch().await? {
                            Event::Existing(fidl_fuchsia_net_interfaces::Properties {
                                id,
                                online,
                                device_class,
                                ..
                            })
                            | Event::Added(fidl_fuchsia_net_interfaces::Properties {
                                id,
                                online,
                                device_class,
                                ..
                            }) => {
                                // Note: if multiple wlan interfaces are available, select the first one come online,
                                // regardless whether it has an internet connection.
                                fx_log_info!("Looking for backbone if: id {:?} online {:?} device_class {:?}", id, online, device_class);
                                if let (
                                    Some(fidl_fuchsia_net_interfaces::DeviceClass::Device(
                                        fidl_fuchsia_hardware_network::DeviceClass::Wlan,
                                    )),
                                    Some(id),
                                ) = (device_class, id)
                                {
                                    wlan_nicid_set.insert(id);
                                }

                                if let (Some(id), Some(true)) = (id, online) {
                                    if wlan_nicid_set.contains(&id) {
                                        fx_log_info!(
                                            "Looking for backbone if: wlan client is online"
                                        );
                                        return Err(anyhow::Error::from(BackboneNetworkChanged));
                                    }
                                }
                            }

                            Event::Changed(fidl_fuchsia_net_interfaces::Properties {
                                id,
                                online,
                                device_class,
                                ..
                            }) => {
                                // Note: if multiple wlan interfaces are available, select the first one come online,
                                // regardless whether it has an internet connection.
                                fx_log_info!("Looking for backbone if: id {:?} online {:?} device_class {:?}", id, online, device_class);
                                if let (Some(id), Some(true)) = (id, online) {
                                    if wlan_nicid_set.contains(&id) {
                                        fx_log_info!(
                                            "Looking for backbone if: wlan client is online"
                                        );
                                        return Err(anyhow::Error::from(BackboneNetworkChanged));
                                    }
                                }
                            }

                            _ => continue,
                        }
                    }
                }
            },
        );

        watch_all_if_events_stream.boxed()
    }

    // only return Ok when the interface is up or down
    // for add/remove of the interface, return Err which will bring down the lowpan-ot-driver
    fn watch_existing_backbone_if_boxed_stream(&self) -> BoxStream<'_, Result<bool, Error>> {
        struct EventState {
            prev_prop: Properties,
            watcher: Option<WatcherProxy>,
            pending_events: Vec<bool>,
        }

        let init_state = EventState {
            prev_prop: Properties::EMPTY,
            watcher: None,
            pending_events: Vec::default(),
        };

        let backbone_if_event_stream = futures::stream::try_unfold(init_state, move |mut state| {
            async move {
                if state.watcher.is_none() {
                    let fnif_state = connect_to_protocol::<StateMarker>()?;
                    let (watcher, req) = create_proxy::<WatcherMarker>()?;
                    fnif_state.get_watcher(WatcherOptions::EMPTY, req)?;
                    state.watcher = Some(watcher);
                }

                loop {
                    // Received interface up/down event, return
                    if let Some(event) = state.pending_events.pop() {
                        return Ok(Some((event, state)));
                    }

                    match state.watcher.as_ref().unwrap().watch().await? {
                        Event::Existing(prop) if prop.id == Some(self.id) => {
                            assert!(
                                state.prev_prop.id == None,
                                "Got `Event::Existing` twice for same interface"
                            );
                            state.prev_prop = prop;
                            continue;
                        }
                        Event::Idle(_) => {
                            if state.prev_prop.id == None {
                                // The interface has been removed before the watcher is setup
                                // need to restart lowpan-ot-driver
                                return Err(format_err!("Interface no longer exists"));
                            }
                        }

                        Event::Removed(id) if id == self.id => {
                            return Err(format_err!("Interface removed"))
                        }

                        Event::Changed(prop) if prop.id == Some(self.id) => {
                            assert!(state.prev_prop.id.is_some());

                            traceln!("BackboneInterface: Got Event::Changed({:#?})", prop);
                            if let Some(online) = prop.online.as_ref() {
                                state.pending_events.push(*online);
                            }

                            traceln!(
                                "BackboneInterface: Queued events: {:#?}",
                                state.pending_events
                            );

                            state.prev_prop = prop;
                        }

                        _ => continue,
                    }
                }
            }
        });

        backbone_if_event_stream.boxed()
    }
}

#[async_trait::async_trait]
impl BackboneInterface for BackboneNetworkInterface {
    fn get_nicid(&self) -> u64 {
        self.id
    }

    fn event_stream(&self) -> BoxStream<'_, Result<bool, Error>> {
        match self.id {
            0 => self.watch_for_new_backbone_if_boxed_stream(),
            _ => self.watch_existing_backbone_if_boxed_stream(),
        }
    }

    async fn is_backbone_if_running(&self) -> bool {
        let fnif_state = connect_to_protocol::<StateMarker>().expect("connect to StateMarker");
        let (watcher, req) = create_proxy::<WatcherMarker>().expect("creating WatcherMarker proxy");
        fnif_state.get_watcher(WatcherOptions::EMPTY, req).expect("getting watcher");

        loop {
            match watcher.watch().await.expect("watcher.watch") {
                Event::Existing(prop) if prop.id == Some(self.id) => {
                    if let Some(true) = prop.online {
                        return true;
                    } else {
                        return false;
                    }
                }
                Event::Idle(_) => {
                    break;
                }
                _ => continue,
            }
        }
        false
    }
}

#[derive(Debug)]
pub struct DummyBackboneInterface {
    id: u64,
}

impl Default for DummyBackboneInterface {
    fn default() -> Self {
        DummyBackboneInterface { id: 1 }
    }
}

#[async_trait::async_trait]
impl BackboneInterface for DummyBackboneInterface {
    fn get_nicid(&self) -> u64 {
        self.id
    }

    fn event_stream(&self) -> BoxStream<'_, Result<bool, Error>> {
        futures::future::pending().into_stream().boxed()
    }

    async fn is_backbone_if_running(&self) -> bool {
        true
    }
}

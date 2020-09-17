// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use std::collections::HashMap;
use thiserror::Error;

use fidl_fuchsia_net_interfaces as fidl_interfaces;

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// Manual implementation of `Clone`.
pub trait CloneExt {
    /// Returns a copy of the value.
    fn clone(&self) -> Self;
}

// This will no longer be necessary when Clone can be derived for FIDL unions
// that don't store handles.
impl CloneExt for fidl_interfaces::Event {
    fn clone(&self) -> Self {
        match self {
            Self::Added(properties) => Self::Added(properties.clone()),
            Self::Existing(properties) => Self::Existing(properties.clone()),
            Self::Idle(empty) => Self::Idle(*empty),
            Self::Changed(properties) => Self::Changed(properties.clone()),
            Self::Removed(id) => Self::Removed(*id),
        }
    }
}

// This will no longer be necessary when Clone can be derived for FIDL tables
// that don't store handles.
impl CloneExt for fidl_interfaces::Properties {
    fn clone(&self) -> Self {
        fidl_interfaces::Properties {
            addresses: self
                .addresses
                .as_ref()
                .map(|addresses| addresses.iter().map(|a| a.clone()).collect()),
            device_class: self.device_class.as_ref().map(|c| c.clone()),
            name: self.name.clone(),
            ..*self
        }
    }
}

// This will no longer be necessary when Clone can be derived for FIDL tables
// that don't store handles.
impl CloneExt for fidl_interfaces::Address {
    fn clone(&self) -> Self {
        fidl_interfaces::Address { ..*self }
    }
}

// This will no longer be necessary when Clone can be derived for FIDL unions
// that don't store handles.
impl CloneExt for fidl_interfaces::DeviceClass {
    fn clone(&self) -> Self {
        match self {
            Self::Loopback(empty) => Self::Loopback(*empty),
            Self::Device(c) => Self::Device(*c),
        }
    }
}

fn apply_change(
    properties: &mut fidl_interfaces::Properties,
    fidl_interfaces::Properties {
        id,
        name: _,
        device_class: _,
        online,
        has_default_ipv4_route,
        has_default_ipv6_route,
        addresses,
    }: fidl_interfaces::Properties,
) {
    if properties.id == id {
        if online.is_some() {
            properties.online = online;
        }
        if has_default_ipv4_route.is_some() {
            properties.has_default_ipv4_route = has_default_ipv4_route;
        }
        if has_default_ipv6_route.is_some() {
            properties.has_default_ipv6_route = has_default_ipv6_route;
        }
        if addresses.is_some() {
            properties.addresses = addresses;
        }
    }
}

fn immutable_fields_present(properties: &fidl_interfaces::Properties) -> bool {
    if let fidl_interfaces::Properties {
        id: Some(_),
        name: Some(_),
        device_class: Some(_),
        online: _,
        addresses: _,
        has_default_ipv4_route: _,
        has_default_ipv6_route: _,
    } = properties
    {
        true
    } else {
        false
    }
}

/// Error returned by `Update::update`.
#[derive(Error, Debug, PartialEq)]
pub enum UpdateError {
    #[error("state is invalid and cannot be updated")]
    InvalidState,
    #[error("duplicate added event {0:?}")]
    DuplicateAdded(fidl_interfaces::Properties),
    #[error("duplicate existing event {0:?}")]
    DuplicateExisting(fidl_interfaces::Properties),
    #[error("unknown interface changed {0:?}")]
    UnknownChanged(fidl_interfaces::Properties),
    #[error("unknown interface with id {0} deleted")]
    UnknownRemoved(u64),
    #[error("added/existing event with missing immutable property {0:?}")]
    MissingProperty(fidl_interfaces::Properties),
    #[error("changed event with missing ID {0:?}")]
    MissingId(fidl_interfaces::Properties),
    #[error("interface has been removed")]
    Removed,
}

/// A trait for types holding interface state that can be updated by change events.
pub trait Update {
    /// Update state with the interface change event.
    ///
    /// Returns a bool indicating whether the update caused any changes.
    fn update(&mut self, event: fidl_interfaces::Event) -> std::result::Result<bool, UpdateError>;
}

impl Update for fidl_interfaces::Properties {
    fn update(&mut self, event: fidl_interfaces::Event) -> std::result::Result<bool, UpdateError> {
        if self.id.is_none() {
            return Err(UpdateError::InvalidState);
        }
        match event {
            fidl_interfaces::Event::Existing(existing) => {
                if self.id == existing.id {
                    if !immutable_fields_present(&existing) {
                        return Err(UpdateError::MissingProperty(existing));
                    }
                    if immutable_fields_present(self) {
                        return Err(UpdateError::DuplicateExisting(existing));
                    }
                    *self = existing;
                    Ok(true)
                } else {
                    Ok(false)
                }
            }
            fidl_interfaces::Event::Added(added) => {
                if self.id == added.id {
                    if !immutable_fields_present(&added) {
                        return Err(UpdateError::MissingProperty(added));
                    }
                    if immutable_fields_present(self) {
                        return Err(UpdateError::DuplicateAdded(added));
                    }
                    *self = added;
                    Ok(true)
                } else {
                    Ok(false)
                }
            }
            fidl_interfaces::Event::Changed(change) => {
                if self.id == change.id {
                    if !immutable_fields_present(self) {
                        return Err(UpdateError::UnknownChanged(change));
                    }
                    apply_change(self, change);
                    Ok(true)
                } else {
                    Ok(false)
                }
            }
            fidl_interfaces::Event::Removed(removed_id) => {
                if self.id == Some(removed_id) {
                    if !immutable_fields_present(self) {
                        Err(UpdateError::UnknownRemoved(removed_id))
                    } else {
                        Err(UpdateError::Removed)
                    }
                } else {
                    Ok(false)
                }
            }
            fidl_interfaces::Event::Idle(fidl_interfaces::Empty {}) => Ok(false),
        }
    }
}

impl Update for HashMap<u64, fidl_interfaces::Properties> {
    fn update(&mut self, event: fidl_interfaces::Event) -> std::result::Result<bool, UpdateError> {
        match event {
            fidl_interfaces::Event::Existing(existing) => {
                let id = if let Some(id) = existing.id {
                    id
                } else {
                    return Err(UpdateError::MissingProperty(existing));
                };
                if !immutable_fields_present(&existing) {
                    return Err(UpdateError::MissingProperty(existing));
                }
                if self.contains_key(&id) {
                    return Err(UpdateError::DuplicateExisting(existing));
                }
                self.insert(id, existing);
            }
            fidl_interfaces::Event::Added(added) => {
                let id = if let Some(id) = added.id {
                    id
                } else {
                    return Err(UpdateError::MissingProperty(added));
                };
                if !immutable_fields_present(&added) {
                    return Err(UpdateError::MissingProperty(added));
                }
                if self.contains_key(&id) {
                    return Err(UpdateError::DuplicateAdded(added));
                }
                self.insert(id, added);
            }
            fidl_interfaces::Event::Changed(change) => {
                let id = if let Some(id) = change.id {
                    id
                } else {
                    return Err(UpdateError::MissingId(change));
                };
                if let Some(properties) = self.get_mut(&id).as_mut() {
                    apply_change(properties, change)
                } else {
                    return Err(UpdateError::UnknownChanged(change));
                }
            }
            fidl_interfaces::Event::Removed(removed_id) => {
                if self.remove(&removed_id).is_none() {
                    return Err(UpdateError::UnknownRemoved(removed_id));
                }
            }
            fidl_interfaces::Event::Idle(fidl_interfaces::Empty {}) => {
                return Ok(false);
            }
        }
        Ok(true)
    }
}

/// Wait for a condition on interface state to be satisfied.
///
/// With the initial state in `init`, take events from `stream` and update the state, calling
/// `predicate` whenever the state changes. When `predicate` returns `Some(T)`, yield `Ok(T)`.
///
/// Since the state passed via `init` is mutably updated for every event, when this function
/// returns successfully, the state can be used as the initial state in a subsequent call with a
/// stream of events from the same watcher.
pub async fn wait_interface<S, B, F, T>(stream: S, init: &mut B, mut predicate: F) -> Result<T>
where
    B: Update + std::fmt::Debug,
    S: futures::Stream<Item = Result<fidl_interfaces::Event>>,
    F: FnMut(&B) -> Option<T>,
{
    async_utils::fold::try_fold_while(stream, init, |acc, event| {
        futures::future::ready(
            acc.update(event).context("failed to update properties with event").map(|changed| {
                if changed {
                    if let Some(rtn) = predicate(acc) {
                        return async_utils::fold::FoldWhile::Done(rtn);
                    }
                }
                async_utils::fold::FoldWhile::Continue(acc)
            }),
        )
    })
    .await
    .context("watcher event stream error")?
    .short_circuited()
    .map_err(|final_state| {
        anyhow::anyhow!("watcher event stream ended; final state: {:?}", final_state)
    })
}

const fn empty_properties() -> fidl_interfaces::Properties {
    fidl_interfaces::Properties {
        id: None,
        name: None,
        device_class: None,
        online: None,
        has_default_ipv4_route: None,
        has_default_ipv6_route: None,
        addresses: None,
    }
}

/// An interface's properties if known, or its ID if not yet known.
#[derive(PartialEq, Debug)]
pub enum InterfaceState {
    Unknown(u64),
    Known(fidl_interfaces::Properties),
}

/// Wait for a condition on a specific interface to be satisfied.
///
/// With the initial state in `init`, take events from `stream` and update the state, calling
/// `predicate` whenever the state changes. When `predicate` returns `Some(T)`, yield `Ok(T)`.
///
/// Since the state passed via `init` is mutably updated for every event, when this function
/// returns successfully, the state can be used as the initial state in a subsequent call with a
/// stream of events from the same watcher.
pub async fn wait_interface_with_id<S, F, T>(
    stream: S,
    init: &mut InterfaceState,
    predicate: F,
) -> Result<T>
where
    S: futures::Stream<Item = Result<fidl_interfaces::Event>>,
    F: FnMut(&fidl_interfaces::Properties) -> Option<T>,
{
    match init {
        InterfaceState::Unknown(id) => {
            let mut properties =
                fidl_interfaces::Properties { id: Some(*id), ..empty_properties() };
            let rtn = wait_interface(stream, &mut properties, predicate).await?;
            *init = InterfaceState::Known(properties);
            Ok(rtn)
        }
        InterfaceState::Known(ref mut properties) => {
            wait_interface(stream, properties, predicate).await
        }
    }
}

/// Returns a stream of interface change events obtained by repeatedly calling watch on `watcher`.
pub fn event_stream(
    watcher: fidl_interfaces::WatcherProxy,
) -> impl futures::Stream<Item = Result<fidl_interfaces::Event>> {
    futures::stream::try_unfold(watcher, |watcher| async {
        Ok(Some((watcher.watch().await.context("failed to watch")?, watcher)))
    })
}

/// Initialize a watcher and return its events as a stream.
pub fn event_stream_from_state(
    interface_state: &fidl_interfaces::StateProxy,
) -> Result<impl futures::Stream<Item = Result<fidl_interfaces::Event>>> {
    let (watcher, server) = ::fidl::endpoints::create_proxy::<fidl_interfaces::WatcherMarker>()
        .context("failed to create watcher proxy")?;
    let () = interface_state
        .get_watcher(fidl_interfaces::WatcherOptions {}, server)
        .context("failed to initialize interface watcher")?;
    Ok(futures::stream::try_unfold(watcher, |watcher| async {
        Ok(Some((watcher.watch().await.context("failed to watch")?, watcher)))
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use net_declare::fidl_ip;

    fn test_properties(id: u64) -> fidl_interfaces::Properties {
        fidl_interfaces::Properties {
            id: Some(id),
            name: Some("test1".to_string()),
            device_class: Some(fidl_interfaces::DeviceClass::Loopback(fidl_interfaces::Empty {})),
            online: Some(false),
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(false),
            addresses: Some(vec![]),
        }
    }

    const ID: u64 = 1;

    #[test]
    fn test_invalid_state_error() {
        assert_eq!(
            empty_properties().update(fidl_interfaces::Event::Existing(test_properties(ID))),
            Err(UpdateError::InvalidState)
        );
    }

    #[test]
    fn test_duplicate_added_error() {
        let mut properties = test_properties(ID);
        let mut properties_map = HashMap::new();
        properties_map.insert(ID, properties.clone());
        let event = fidl_interfaces::Event::Added(properties.clone());
        assert_eq!(
            properties.update(event.clone()),
            Err(UpdateError::DuplicateAdded(properties.clone()))
        );
        assert_eq!(properties_map.update(event), Err(UpdateError::DuplicateAdded(properties)));
    }

    #[test]
    fn test_duplicate_existing_error() {
        let mut properties = test_properties(ID);
        let mut properties_map = HashMap::new();
        properties_map.insert(ID, properties.clone());
        let event = fidl_interfaces::Event::Existing(properties.clone());
        assert_eq!(
            properties.update(event.clone()),
            Err(UpdateError::DuplicateExisting(properties.clone()))
        );
        assert_eq!(properties_map.update(event), Err(UpdateError::DuplicateExisting(properties)));
    }

    #[test]
    fn test_unknown_changed_error() {
        let unknown_changed =
            fidl_interfaces::Properties { id: Some(ID), online: Some(true), ..empty_properties() };
        let event = fidl_interfaces::Event::Changed(unknown_changed.clone());
        assert_eq!(
            fidl_interfaces::Properties { id: Some(ID), ..empty_properties() }
                .update(event.clone()),
            Err(UpdateError::UnknownChanged(unknown_changed.clone()))
        );
        assert_eq!(HashMap::new().update(event), Err(UpdateError::UnknownChanged(unknown_changed)));
    }

    #[test]
    fn test_unknown_removed_error() {
        assert_eq!(
            fidl_interfaces::Properties { id: Some(ID), ..empty_properties() }
                .update(fidl_interfaces::Event::Removed(ID)),
            Err(UpdateError::UnknownRemoved(ID))
        );
        assert_eq!(
            HashMap::new().update(fidl_interfaces::Event::Removed(ID)),
            Err(UpdateError::UnknownRemoved(ID))
        );
    }

    #[test]
    fn test_missing_property_error() {
        let missing_property = fidl_interfaces::Properties { name: None, ..test_properties(ID) };
        let event = fidl_interfaces::Event::Existing(missing_property.clone());
        assert_eq!(
            fidl_interfaces::Properties { id: Some(ID), ..empty_properties() }
                .update(event.clone()),
            Err(UpdateError::MissingProperty(missing_property.clone()))
        );
        assert_eq!(
            HashMap::new().update(event),
            Err(UpdateError::MissingProperty(missing_property))
        );
    }

    #[test]
    fn test_missing_id_error() {
        let missing_id = fidl_interfaces::Properties { online: Some(true), ..empty_properties() };
        assert_eq!(
            HashMap::new().update(fidl_interfaces::Event::Changed(missing_id.clone())),
            Err(UpdateError::MissingId(missing_id))
        );
    }

    #[test]
    fn test_removed_error() {
        assert_eq!(
            test_properties(ID).update(fidl_interfaces::Event::Removed(ID)),
            Err(UpdateError::Removed)
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wait_one_interface() -> Result {
        let mut properties = InterfaceState::Unknown(ID);
        let addr = fidl_interfaces::Address {
            addr: Some(fidl_fuchsia_net::Subnet { addr: fidl_ip!(192.168.0.1), prefix_len: 16 }),
        };
        for (event, want) in vec![
            (fidl_interfaces::Event::Existing(test_properties(ID)), test_properties(ID)),
            (
                fidl_interfaces::Event::Changed(fidl_interfaces::Properties {
                    id: Some(ID),
                    online: Some(true),
                    ..empty_properties()
                }),
                fidl_interfaces::Properties { online: Some(true), ..test_properties(ID) },
            ),
            (
                fidl_interfaces::Event::Changed(fidl_interfaces::Properties {
                    id: Some(ID),
                    addresses: Some(vec![addr.clone()]),
                    ..empty_properties()
                }),
                fidl_interfaces::Properties {
                    online: Some(true),
                    addresses: Some(vec![addr]),
                    ..test_properties(ID)
                },
            ),
        ] {
            let () = wait_interface_with_id(
                futures::stream::once(futures::future::ok(event)),
                &mut properties,
                |got| {
                    assert_eq!(*got, want);
                    Some(())
                },
            )
            .await?;
            assert_eq!(properties, InterfaceState::Known(want));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wait_all_interfaces() -> Result {
        const ID2: u64 = 2;
        let mut properties_map = HashMap::new();
        for (event, want) in vec![
            (
                fidl_interfaces::Event::Existing(test_properties(ID)),
                vec![(ID, test_properties(ID))].into_iter().collect(),
            ),
            (
                fidl_interfaces::Event::Added(test_properties(ID2)),
                vec![(ID, test_properties(ID)), (ID2, test_properties(ID2))].into_iter().collect(),
            ),
            (
                fidl_interfaces::Event::Changed(fidl_interfaces::Properties {
                    id: Some(ID),
                    online: Some(true),
                    ..empty_properties()
                }),
                vec![
                    (ID, fidl_interfaces::Properties { online: Some(true), ..test_properties(ID) }),
                    (ID2, test_properties(ID2)),
                ]
                .into_iter()
                .collect(),
            ),
            (
                fidl_interfaces::Event::Removed(ID),
                vec![(ID2, test_properties(ID2))].into_iter().collect(),
            ),
        ] {
            let () = wait_interface(
                futures::stream::once(futures::future::ok(event)),
                &mut properties_map,
                |got| {
                    assert_eq!(*got, want);
                    Some(())
                },
            )
            .await?;
            assert_eq!(properties_map, want);
        }
        Ok(())
    }
}

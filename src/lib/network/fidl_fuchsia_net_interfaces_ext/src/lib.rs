// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for the fuchsia.net.interfaces FIDL library.

#![deny(missing_docs)]

mod reachability;

pub use reachability::{to_reachability_stream, wait_for_reachability};

use fidl_table_validation::*;
use futures::{Stream, TryStreamExt as _};
use std::collections::hash_map::{self, HashMap};
use std::convert::TryFrom as _;
use thiserror::Error;

use fidl_fuchsia_net_interfaces as fnet_interfaces;

// TODO(fxbug.dev/66175) Prevent this type from becoming stale.
/// Properties of a network interface.
#[derive(Clone, Debug, Eq, PartialEq, ValidFidlTable)]
#[fidl_table_src(fnet_interfaces::Properties)]
pub struct Properties {
    /// An opaque identifier for the interface. Its value will not be reused
    /// even if the device is removed and subsequently re-added. Immutable.
    pub id: u64,
    /// The name of the interface. Immutable.
    pub name: String,
    /// The device class of the interface. Immutable.
    pub device_class: fnet_interfaces::DeviceClass,
    /// The device is enabled and its physical state is online.
    pub online: bool,
    /// The addresses currently assigned to the interface.
    pub addresses: Vec<Address>,
    /// Whether there is a default IPv4 route through this interface.
    pub has_default_ipv4_route: bool,
    /// Whether there is a default IPv6 route through this interface.
    pub has_default_ipv6_route: bool,
}

// TODO(fxbug.dev/66175) Prevent this type from becoming stale.
/// An address and its properties.
#[derive(Clone, Debug, Eq, PartialEq, ValidFidlTable)]
#[fidl_table_src(fnet_interfaces::Address)]
pub struct Address {
    /// The address and prefix length.
    pub addr: fidl_fuchsia_net::Subnet,
}

/// Interface watcher event update errors.
#[derive(Error, Debug)]
pub enum UpdateError {
    /// The update attempted to add an already-known added interface into local state.
    #[error("duplicate added event {0:?}")]
    DuplicateAdded(fnet_interfaces::Properties),
    /// The update attempted to add an already-known existing interface into local state.
    #[error("duplicate existing event {0:?}")]
    DuplicateExisting(fnet_interfaces::Properties),
    /// The event contained one or more invalid properties.
    #[error("failed to validate Properties FIDL table: {0}")]
    InvalidProperties(#[from] PropertiesValidationError),
    /// The event contained one or more invalid addresses.
    #[error("failed to validate Address FIDL table: {0}")]
    InvalidAddress(#[from] AddressValidationError),
    /// The event was required to have contained an ID, but did not.
    #[error("changed event with missing ID {0:?}")]
    MissingId(fnet_interfaces::Properties),
    /// The event did not contain any changes.
    #[error("changed event contains no changed fields {0:?}")]
    EmptyChange(fnet_interfaces::Properties),
    /// The update removed the only interface in the local state.
    #[error("interface has been removed")]
    Removed,
    /// The event contained changes for an interface that did not exist in local state.
    #[error("unknown interface changed {0:?}")]
    UnknownChanged(fnet_interfaces::Properties),
    /// The event removed an interface that did not exist in local state.
    #[error("unknown interface with id {0} deleted")]
    UnknownRemoved(u64),
}

/// The result of updating network interface state with an event.
#[derive(Debug, Eq, PartialEq)]
pub enum UpdateResult<'a> {
    /// The update did not change the local state.
    NoChange,
    /// The update inserted an existing interface into the local state.
    Existing(&'a Properties),
    /// The update inserted an added interface into the local state.
    Added(&'a Properties),
    /// The update changed an existing interface in the local state.
    Changed(&'a Properties),
    /// The update removed a removed interface from the local state.
    Removed(u64),
}

/// A trait for types holding interface state that can be updated by change events.
pub trait Update {
    /// Update state with the interface change event.
    ///
    /// Returns a bool indicating whether the update caused any changes.
    fn update(&mut self, event: fnet_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError>;
}

impl Update for Properties {
    fn update(&mut self, event: fnet_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError> {
        match event {
            fnet_interfaces::Event::Existing(existing) => {
                let existing = Properties::try_from(existing)?;
                if existing.id == self.id {
                    return Err(UpdateError::DuplicateExisting(existing.into()));
                }
            }
            fnet_interfaces::Event::Added(added) => {
                let added = Properties::try_from(added)?;
                if added.id == self.id {
                    return Err(UpdateError::DuplicateAdded(added.into()));
                }
            }
            fnet_interfaces::Event::Changed(change) => {
                if let Some(id) = change.id {
                    if self.id == id {
                        let mut changed = false;
                        if let Some(online) = change.online {
                            self.online = online;
                            changed = true;
                        }
                        if let Some(has_default_ipv4_route) = change.has_default_ipv4_route {
                            self.has_default_ipv4_route = has_default_ipv4_route;
                            changed = true;
                        }
                        if let Some(has_default_ipv6_route) = change.has_default_ipv6_route {
                            self.has_default_ipv6_route = has_default_ipv6_route;
                            changed = true;
                        }
                        if let Some(addresses) = change.addresses {
                            self.addresses = addresses
                                .into_iter()
                                .map(Address::try_from)
                                .collect::<Result<Vec<_>, _>>()?;
                        } else if !changed {
                            return Err(UpdateError::EmptyChange(change));
                        }
                        return Ok(UpdateResult::Changed(self));
                    }
                } else {
                    return Err(UpdateError::MissingId(change));
                }
            }
            fnet_interfaces::Event::Removed(removed_id) => {
                if self.id == removed_id {
                    return Err(UpdateError::Removed);
                }
            }
            fnet_interfaces::Event::Idle(fnet_interfaces::Empty {}) => {}
        }
        Ok(UpdateResult::NoChange)
    }
}

impl Update for InterfaceState {
    fn update(&mut self, event: fnet_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError> {
        fn get_properties(state: &InterfaceState) -> &Properties {
            match state {
                InterfaceState::Known(properties) => properties,
                InterfaceState::Unknown(id) => unreachable!(
                    "matched `Unknown({})` immediately after assigning with `Known`",
                    id
                ),
            }
        }
        match self {
            InterfaceState::Unknown(id) => match event {
                fnet_interfaces::Event::Existing(existing) => {
                    let existing = Properties::try_from(existing)?;
                    if existing.id == *id {
                        *self = InterfaceState::Known(existing);
                        return Ok(UpdateResult::Existing(get_properties(self)));
                    }
                }
                fnet_interfaces::Event::Added(added) => {
                    let added = Properties::try_from(added)?;
                    if added.id == *id {
                        *self = InterfaceState::Known(added);
                        return Ok(UpdateResult::Added(get_properties(self)));
                    }
                }
                fnet_interfaces::Event::Changed(change) => {
                    if let Some(change_id) = change.id {
                        if change_id == *id {
                            return Err(UpdateError::UnknownChanged(change));
                        }
                    } else {
                        return Err(UpdateError::MissingId(change));
                    }
                }
                fnet_interfaces::Event::Removed(removed_id) => {
                    if removed_id == *id {
                        return Err(UpdateError::UnknownRemoved(removed_id));
                    }
                }
                fnet_interfaces::Event::Idle(fnet_interfaces::Empty {}) => {}
            },
            InterfaceState::Known(properties) => return properties.update(event),
        }
        Ok(UpdateResult::NoChange)
    }
}

impl Update for HashMap<u64, Properties> {
    fn update(&mut self, event: fnet_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError> {
        match event {
            fnet_interfaces::Event::Existing(existing) => {
                let existing = Properties::try_from(existing)?;
                match self.entry(existing.id) {
                    hash_map::Entry::Occupied(_) => {
                        Err(UpdateError::DuplicateExisting(existing.into()))
                    }
                    hash_map::Entry::Vacant(entry) => {
                        Ok(UpdateResult::Existing(entry.insert(existing)))
                    }
                }
            }
            fnet_interfaces::Event::Added(added) => {
                let added = Properties::try_from(added)?;
                match self.entry(added.id) {
                    hash_map::Entry::Occupied(_) => Err(UpdateError::DuplicateAdded(added.into())),
                    hash_map::Entry::Vacant(entry) => Ok(UpdateResult::Added(entry.insert(added))),
                }
            }
            fnet_interfaces::Event::Changed(change) => {
                let id = if let Some(id) = change.id {
                    id
                } else {
                    return Err(UpdateError::MissingId(change));
                };
                if let Some(properties) = self.get_mut(&id) {
                    properties.update(fnet_interfaces::Event::Changed(change))
                } else {
                    Err(UpdateError::UnknownChanged(change))
                }
            }
            fnet_interfaces::Event::Removed(removed_id) => {
                if self.remove(&removed_id).is_none() {
                    return Err(UpdateError::UnknownRemoved(removed_id));
                }
                Ok(UpdateResult::Removed(removed_id))
            }
            fnet_interfaces::Event::Idle(fnet_interfaces::Empty {}) => Ok(UpdateResult::NoChange),
        }
    }
}

/// Interface watcher operational errors.
#[derive(Error, Debug)]
pub enum WatcherOperationError<B: Update + std::fmt::Debug> {
    /// Watcher event stream yielded an error.
    #[error("event stream error: {0}")]
    EventStream(fidl::Error),
    /// Watcher event stream yielded an event that could not be applied to the local state.
    #[error("failed to update: {0}")]
    Update(UpdateError),
    /// Watcher event stream ended unexpectedly.
    #[error("watcher event stream ended unexpectedly, final state: {final_state:?}")]
    UnexpectedEnd {
        /// The local state at the time of the watcher event stream's end.
        final_state: B,
    },
}

/// Interface watcher creation errors.
#[derive(Error, Debug)]
pub enum WatcherCreationError {
    /// Proxy creation failed.
    #[error("failed to create interface watcher proxy: {0}")]
    CreateProxy(fidl::Error),
    /// Watcher acquisition failed.
    #[error("failed to get interface watcher: {0}")]
    GetWatcher(fidl::Error),
}

/// Wait for a condition on interface state to be satisfied.
///
/// With the initial state in `init`, take events from `stream` and update the state, calling
/// `predicate` whenever the state changes. When `predicate` returns `Some(T)`, yield `Ok(T)`.
///
/// Since the state passed via `init` is mutably updated for every event, when this function
/// returns successfully, the state can be used as the initial state in a subsequent call with a
/// stream of events from the same watcher.
pub async fn wait_interface<B, S, F, T>(
    stream: S,
    init: &mut B,
    mut predicate: F,
) -> Result<T, WatcherOperationError<B>>
where
    B: Update + Clone + std::fmt::Debug,
    S: Stream<Item = Result<fnet_interfaces::Event, fidl::Error>>,
    F: FnMut(&B) -> Option<T>,
{
    async_utils::fold::try_fold_while(
        stream.map_err(WatcherOperationError::EventStream),
        init,
        |acc, event| {
            futures::future::ready(match acc.update(event) {
                Ok(changed) => match changed {
                    UpdateResult::Existing(_)
                    | UpdateResult::Added(_)
                    | UpdateResult::Changed(_)
                    | UpdateResult::Removed(_) => {
                        if let Some(rtn) = predicate(acc) {
                            Ok(async_utils::fold::FoldWhile::Done(rtn))
                        } else {
                            Ok(async_utils::fold::FoldWhile::Continue(acc))
                        }
                    }
                    UpdateResult::NoChange => Ok(async_utils::fold::FoldWhile::Continue(acc)),
                },
                Err(e) => Err(WatcherOperationError::Update(e)),
            })
        },
    )
    .await?
    .short_circuited()
    .map_err(|final_state| WatcherOperationError::UnexpectedEnd {
        final_state: final_state.clone(),
    })
}

/// The local state of an interface's properties.
#[derive(Clone, Debug, PartialEq)]
pub enum InterfaceState {
    /// Not yet known.
    Unknown(u64),
    /// Locally known.
    Known(Properties),
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
    mut predicate: F,
) -> Result<T, WatcherOperationError<InterfaceState>>
where
    S: Stream<Item = Result<fnet_interfaces::Event, fidl::Error>>,
    F: FnMut(&Properties) -> Option<T>,
{
    wait_interface(stream, init, |state| {
        match state {
            InterfaceState::Known(properties) => predicate(properties),
            // NB This is technically unreachable because a successful update will always change
            // `Unknown` to `Known` (and `Known` will stay `Known`).
            InterfaceState::Unknown(_) => None,
        }
    })
    .await
}

/// Returns a stream of interface change events obtained by repeatedly calling watch on `watcher`.
pub fn event_stream(
    watcher: fnet_interfaces::WatcherProxy,
) -> impl Stream<Item = Result<fnet_interfaces::Event, fidl::Error>> {
    futures::stream::try_unfold(watcher, |watcher| async {
        Ok(Some((watcher.watch().await?, watcher)))
    })
}

/// Initialize a watcher and return its events as a stream.
pub fn event_stream_from_state(
    interface_state: &fnet_interfaces::StateProxy,
) -> Result<impl Stream<Item = Result<fnet_interfaces::Event, fidl::Error>>, WatcherCreationError> {
    let (watcher, server) = ::fidl::endpoints::create_proxy::<fnet_interfaces::WatcherMarker>()
        .map_err(WatcherCreationError::CreateProxy)?;
    let () = interface_state
        .get_watcher(fnet_interfaces::WatcherOptions::EMPTY, server)
        .map_err(WatcherCreationError::GetWatcher)?;
    Ok(event_stream(watcher))
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use matches::assert_matches;
    use net_declare::fidl_ip;
    use std::convert::TryInto as _;

    type Result<T = ()> = std::result::Result<T, anyhow::Error>;

    fn fidl_properties(id: u64) -> fnet_interfaces::Properties {
        fnet_interfaces::Properties {
            id: Some(id),
            name: Some("test1".to_string()),
            device_class: Some(fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {})),
            online: Some(false),
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(false),
            addresses: Some(vec![]),
            ..fnet_interfaces::Properties::EMPTY
        }
    }

    fn properties(id: u64) -> Properties {
        fidl_properties(id).try_into().expect("failed to validate FIDL Properties")
    }

    const ID: u64 = 1;

    #[test]
    fn test_duplicate_added_error() {
        assert_matches!(
            properties(ID).update(fnet_interfaces::Event::Added(fidl_properties(ID))),
            Err(UpdateError::DuplicateAdded(added)) if added == fidl_properties(ID)
        );
        assert_matches!(
            std::iter::once((ID, properties(ID)))
                .collect::<HashMap<_, _>>()
                .update(fnet_interfaces::Event::Added(fidl_properties(ID))),
            Err(UpdateError::DuplicateAdded(added)) if added == fidl_properties(ID)
        );
    }

    #[test]
    fn test_duplicate_existing_error() {
        assert_matches!(
            properties(ID).update(fnet_interfaces::Event::Existing(fidl_properties(ID))),
            Err(UpdateError::DuplicateExisting(existing)) if existing == fidl_properties(ID)
        );
        assert_matches!(
            std::iter::once((ID, properties(ID)))
                .collect::<HashMap<_, _>>()
                .update(fnet_interfaces::Event::Existing(fidl_properties(ID))),
            Err(UpdateError::DuplicateExisting(existing)) if existing == fidl_properties(ID)
        );
    }

    #[test]
    fn test_unknown_changed_error() {
        let unknown_changed = || fnet_interfaces::Properties {
            id: Some(ID),
            online: Some(true),
            ..fnet_interfaces::Properties::EMPTY
        };
        assert_matches!(
            InterfaceState::Unknown(ID).update(fnet_interfaces::Event::Changed(unknown_changed())),
            Err(UpdateError::UnknownChanged(changed)) if changed == unknown_changed()
        );
        assert_matches!(
            HashMap::new().update(fnet_interfaces::Event::Changed(unknown_changed())),
            Err(UpdateError::UnknownChanged(changed)) if changed == unknown_changed()
        );
    }

    #[test]
    fn test_unknown_removed_error() {
        assert_matches!(
            InterfaceState::Unknown(ID).update(fnet_interfaces::Event::Removed(ID)),
            Err(UpdateError::UnknownRemoved(id)) if id == ID
        );
        assert_matches!(
            HashMap::new().update(fnet_interfaces::Event::Removed(ID)),
            Err(UpdateError::UnknownRemoved(id)) if id == ID
        );
    }

    #[test]
    fn test_removed_error() {
        assert_matches!(
            properties(ID).update(fnet_interfaces::Event::Removed(ID)),
            Err(UpdateError::Removed)
        );
    }

    #[test]
    fn test_missing_id_error() {
        let missing_id = || fnet_interfaces::Properties {
            online: Some(true),
            ..fnet_interfaces::Properties::EMPTY
        };
        assert_matches!(
            HashMap::new().update(fnet_interfaces::Event::Changed(missing_id())),
            Err(UpdateError::MissingId(properties)) if properties == missing_id()
        );
        assert_matches!(
            properties(ID).update(fnet_interfaces::Event::Changed(missing_id())),
            Err(UpdateError::MissingId(properties)) if properties == missing_id()
        );
        assert_matches!(
            InterfaceState::Unknown(ID).update(fnet_interfaces::Event::Changed(missing_id())),
            Err(UpdateError::MissingId(properties)) if properties == missing_id()
        );
    }

    #[test]
    fn test_empty_change_error() {
        let empty_change =
            || fnet_interfaces::Properties { id: Some(ID), ..fnet_interfaces::Properties::EMPTY };
        assert_matches!(
            properties(ID).update(fnet_interfaces::Event::Changed(empty_change())),
            Err(UpdateError::EmptyChange(properties)) if properties == empty_change()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wait_one_interface() -> Result {
        let mut state = InterfaceState::Unknown(ID);
        let addr = fidl_fuchsia_net::Subnet { addr: fidl_ip!(192.168.0.1), prefix_len: 16 };
        for (event, want) in vec![
            (fnet_interfaces::Event::Existing(fidl_properties(ID)), properties(ID)),
            (
                fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
                    id: Some(ID),
                    online: Some(true),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                Properties::try_from(fnet_interfaces::Properties {
                    online: Some(true),
                    ..fidl_properties(ID)
                })?,
            ),
            (
                fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
                    id: Some(ID),
                    addresses: Some(vec![fnet_interfaces::Address {
                        addr: Some(addr),
                        ..fnet_interfaces::Address::EMPTY
                    }]),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                Properties::try_from(fnet_interfaces::Properties {
                    online: Some(true),
                    addresses: Some(vec![fnet_interfaces::Address {
                        addr: Some(addr),
                        ..fnet_interfaces::Address::EMPTY
                    }]),
                    ..fidl_properties(ID)
                })?,
            ),
        ] {
            let () = wait_interface_with_id(
                futures::stream::once(futures::future::ok(event)),
                &mut state,
                |got| {
                    assert_eq!(*got, want);
                    Some(())
                },
            )
            .await?;
            assert_eq!(state, InterfaceState::Known(want));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wait_all_interfaces() -> Result {
        const ID2: u64 = 2;
        let mut properties_map = HashMap::new();
        for (event, want) in vec![
            (
                fnet_interfaces::Event::Existing(fidl_properties(ID)),
                vec![(ID, properties(ID))].into_iter().collect(),
            ),
            (
                fnet_interfaces::Event::Added(fidl_properties(ID2)),
                vec![(ID, properties(ID)), (ID2, properties(ID2))].into_iter().collect(),
            ),
            (
                fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
                    id: Some(ID),
                    online: Some(true),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                vec![
                    (
                        ID,
                        fnet_interfaces::Properties { online: Some(true), ..fidl_properties(ID) }
                            .try_into()?,
                    ),
                    (ID2, properties(ID2)),
                ]
                .into_iter()
                .collect(),
            ),
            (
                fnet_interfaces::Event::Removed(ID),
                vec![(ID2, properties(ID2))].into_iter().collect(),
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

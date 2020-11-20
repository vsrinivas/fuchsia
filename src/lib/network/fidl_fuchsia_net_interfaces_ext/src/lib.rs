// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod reachability;

pub use reachability::{to_reachability_stream, wait_for_reachability};

use futures::{Stream, TryStreamExt as _};
use std::collections::hash_map::{self, HashMap};
use thiserror::Error;

use fidl_fuchsia_net_interfaces as fidl_interfaces;

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
        ..
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
        ..
    } = properties
    {
        true
    } else {
        false
    }
}

/// Interface watcher event update errors.
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

/// The result of updating network interface state with an event.
#[derive(Debug, PartialEq)]
pub enum UpdateResult<'a> {
    NoChange,
    Existing(&'a fidl_interfaces::Properties),
    Added(&'a fidl_interfaces::Properties),
    Changed(&'a fidl_interfaces::Properties),
    Removed(u64),
}

/// A trait for types holding interface state that can be updated by change events.
pub trait Update {
    /// Update state with the interface change event.
    ///
    /// Returns a bool indicating whether the update caused any changes.
    fn update(&mut self, event: fidl_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError>;
}

impl Update for fidl_interfaces::Properties {
    fn update(&mut self, event: fidl_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError> {
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
                    return Ok(UpdateResult::Existing(self));
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
                    return Ok(UpdateResult::Added(self));
                }
            }
            fidl_interfaces::Event::Changed(change) => {
                if self.id == change.id {
                    if !immutable_fields_present(self) {
                        return Err(UpdateError::UnknownChanged(change));
                    }
                    apply_change(self, change);
                    return Ok(UpdateResult::Changed(self));
                }
            }
            fidl_interfaces::Event::Removed(removed_id) => {
                if self.id == Some(removed_id) {
                    return if !immutable_fields_present(self) {
                        Err(UpdateError::UnknownRemoved(removed_id))
                    } else {
                        Err(UpdateError::Removed)
                    };
                }
            }
            fidl_interfaces::Event::Idle(fidl_interfaces::Empty {}) => {}
        }
        Ok(UpdateResult::NoChange)
    }
}

impl Update for HashMap<u64, fidl_interfaces::Properties> {
    fn update(&mut self, event: fidl_interfaces::Event) -> Result<UpdateResult<'_>, UpdateError> {
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
                match self.entry(id) {
                    hash_map::Entry::Occupied(_) => Err(UpdateError::DuplicateExisting(existing)),
                    hash_map::Entry::Vacant(entry) => {
                        Ok(UpdateResult::Existing(entry.insert(existing)))
                    }
                }
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
                match self.entry(id) {
                    hash_map::Entry::Occupied(_) => Err(UpdateError::DuplicateAdded(added)),
                    hash_map::Entry::Vacant(entry) => Ok(UpdateResult::Added(entry.insert(added))),
                }
            }
            fidl_interfaces::Event::Changed(change) => {
                let id = if let Some(id) = change.id {
                    id
                } else {
                    return Err(UpdateError::MissingId(change));
                };
                if let Some(properties) = self.get_mut(&id) {
                    apply_change(properties, change);
                    Ok(UpdateResult::Changed(properties))
                } else {
                    Err(UpdateError::UnknownChanged(change))
                }
            }
            fidl_interfaces::Event::Removed(removed_id) => {
                if self.remove(&removed_id).is_none() {
                    return Err(UpdateError::UnknownRemoved(removed_id));
                }
                Ok(UpdateResult::Removed(removed_id))
            }
            fidl_interfaces::Event::Idle(fidl_interfaces::Empty {}) => Ok(UpdateResult::NoChange),
        }
    }
}

/// Interface watcher operational errors.
#[derive(Error, Debug)]
pub enum WatcherOperationError<B: Update + std::fmt::Debug> {
    #[error("event stream error: {0}")]
    EventStream(fidl::Error),
    #[error("failed to update: {0}")]
    Update(UpdateError),
    #[error("watcher event stream ended unexpectedly, final state: {final_state:?}")]
    UnexpectedEnd { final_state: B },
}

/// Interface watcher creation errors.
#[derive(Error, Debug)]
pub enum WatcherCreationError {
    #[error("failed to create interface watcher proxy: {0}")]
    CreateProxy(fidl::Error),
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
    S: Stream<Item = Result<fidl_interfaces::Event, fidl::Error>>,
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
) -> Result<T, WatcherOperationError<fidl_interfaces::Properties>>
where
    S: Stream<Item = Result<fidl_interfaces::Event, fidl::Error>>,
    F: FnMut(&fidl_interfaces::Properties) -> Option<T>,
{
    match init {
        InterfaceState::Unknown(id) => {
            let mut properties = fidl_interfaces::Properties {
                id: Some(*id),
                ..fidl_interfaces::Properties::empty()
            };
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
) -> impl Stream<Item = Result<fidl_interfaces::Event, fidl::Error>> {
    futures::stream::try_unfold(watcher, |watcher| async {
        Ok(Some((watcher.watch().await?, watcher)))
    })
}

/// Initialize a watcher and return its events as a stream.
pub fn event_stream_from_state(
    interface_state: &fidl_interfaces::StateProxy,
) -> Result<impl Stream<Item = Result<fidl_interfaces::Event, fidl::Error>>, WatcherCreationError> {
    let (watcher, server) = ::fidl::endpoints::create_proxy::<fidl_interfaces::WatcherMarker>()
        .map_err(WatcherCreationError::CreateProxy)?;
    let () = interface_state
        .get_watcher(fidl_interfaces::WatcherOptions::empty(), server)
        .map_err(WatcherCreationError::GetWatcher)?;
    Ok(event_stream(watcher))
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use net_declare::fidl_ip;

    type Result<T = ()> = std::result::Result<T, anyhow::Error>;

    fn test_properties(id: u64) -> fidl_interfaces::Properties {
        fidl_interfaces::Properties {
            id: Some(id),
            name: Some("test1".to_string()),
            device_class: Some(fidl_interfaces::DeviceClass::Loopback(fidl_interfaces::Empty {})),
            online: Some(false),
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(false),
            addresses: Some(vec![]),
            ..fidl_interfaces::Properties::empty()
        }
    }

    const ID: u64 = 1;

    #[test]
    fn test_invalid_state_error() {
        assert_eq!(
            fidl_interfaces::Properties::empty()
                .update(fidl_interfaces::Event::Existing(test_properties(ID))),
            Err(UpdateError::InvalidState)
        );
    }

    #[test]
    fn test_duplicate_added_error() {
        let mut properties_map = HashMap::new();
        properties_map.insert(ID, test_properties(ID).clone());
        let event = fidl_interfaces::Event::Added(test_properties(ID).clone());
        assert_eq!(
            test_properties(ID).update(event.clone()),
            Err(UpdateError::DuplicateAdded(test_properties(ID).clone()))
        );
        assert_eq!(
            properties_map.update(event),
            Err(UpdateError::DuplicateAdded(test_properties(ID)))
        );
    }

    #[test]
    fn test_duplicate_existing_error() {
        let mut properties_map = HashMap::new();
        properties_map.insert(ID, test_properties(ID).clone());
        let event = fidl_interfaces::Event::Existing(test_properties(ID).clone());
        assert_eq!(
            test_properties(ID).update(event.clone()),
            Err(UpdateError::DuplicateExisting(test_properties(ID).clone()))
        );
        assert_eq!(
            properties_map.update(event),
            Err(UpdateError::DuplicateExisting(test_properties(ID)))
        );
    }

    #[test]
    fn test_unknown_changed_error() {
        let unknown_changed = fidl_interfaces::Properties {
            id: Some(ID),
            online: Some(true),
            ..fidl_interfaces::Properties::empty()
        };
        let event = fidl_interfaces::Event::Changed(unknown_changed.clone());
        assert_eq!(
            fidl_interfaces::Properties { id: Some(ID), ..fidl_interfaces::Properties::empty() }
                .update(event.clone()),
            Err(UpdateError::UnknownChanged(unknown_changed.clone()))
        );
        assert_eq!(HashMap::new().update(event), Err(UpdateError::UnknownChanged(unknown_changed)));
    }

    #[test]
    fn test_unknown_removed_error() {
        assert_eq!(
            fidl_interfaces::Properties { id: Some(ID), ..fidl_interfaces::Properties::empty() }
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
            fidl_interfaces::Properties { id: Some(ID), ..fidl_interfaces::Properties::empty() }
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
        let missing_id = fidl_interfaces::Properties {
            online: Some(true),
            ..fidl_interfaces::Properties::empty()
        };
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
            ..fidl_interfaces::Address::empty()
        };
        for (event, want) in vec![
            (fidl_interfaces::Event::Existing(test_properties(ID)), test_properties(ID)),
            (
                fidl_interfaces::Event::Changed(fidl_interfaces::Properties {
                    id: Some(ID),
                    online: Some(true),
                    ..fidl_interfaces::Properties::empty()
                }),
                fidl_interfaces::Properties { online: Some(true), ..test_properties(ID) },
            ),
            (
                fidl_interfaces::Event::Changed(fidl_interfaces::Properties {
                    id: Some(ID),
                    addresses: Some(vec![addr.clone()]),
                    ..fidl_interfaces::Properties::empty()
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
                    ..fidl_interfaces::Properties::empty()
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

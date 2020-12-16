// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventError, EventErrorPayload, EventPayload},
        realm::{Component, Realm, RealmState, WeakExtendedRealm},
        resolver::Resolver,
    },
    moniker::ChildMoniker,
    std::convert::TryFrom,
    std::sync::Arc,
};

pub(super) async fn do_resolve(realm: &Arc<Realm>) -> Result<Component, ModelError> {
    let result = async move {
        let component = realm
            .environment
            .resolve(&realm.component_url)
            .await
            .map_err(|err| ModelError::from(err))?;
        let component = Component::try_from(component)?;
        let created_new_realm_state = {
            let mut state = realm.lock_state().await;
            if state.is_none() {
                *state = Some(RealmState::new(realm, component.decl.clone()).await?);
                true
            } else {
                false
            }
        };
        Ok((created_new_realm_state, component))
    }
    .await;

    // If a `RealmState` was installed in this call, first dispatch
    // `Resolved` for the component itrealm and then dispatch
    // `Discovered` for every static child that was discovered in the
    // manifest.
    match result {
        Ok((false, component)) => Ok(component),
        Ok((true, component)) => {
            if let WeakExtendedRealm::AboveRoot(_) = &realm.parent {
                let event = Event::new(realm, Ok(EventPayload::Discovered));
                realm.hooks.dispatch(&event).await?;
            }
            let event =
                Event::new(realm, Ok(EventPayload::Resolved { decl: component.decl.clone() }));
            realm.hooks.dispatch(&event).await?;
            for child in component.decl.children.iter() {
                let child_moniker = ChildMoniker::new(child.name.clone(), None, 0);
                let child_abs_moniker = realm.abs_moniker.child(child_moniker);
                let event = Event::child_discovered(child_abs_moniker, child.url.clone());
                realm.hooks.dispatch(&event).await?;
            }
            Ok(component)
        }
        Err(e) => {
            let event = Event::new(realm, Err(EventError::new(&e, EventErrorPayload::Resolved)));
            realm.hooks.dispatch(&event).await?;
            Err(e)
        }
    }
}

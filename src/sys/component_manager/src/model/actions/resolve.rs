// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{ActionSet, DiscoverAction},
        error::ModelError,
        hooks::{Event, EventError, EventErrorPayload, EventPayload},
        realm::{Component, Realm, RealmState, ResolvedRealmState},
        resolver::Resolver,
    },
    std::convert::TryFrom,
    std::sync::Arc,
};

pub(super) async fn do_resolve(realm: &Arc<Realm>) -> Result<Component, ModelError> {
    // Ensure `Resolved` is dispatched after `Discovered`.
    ActionSet::register(realm.clone(), DiscoverAction::new()).await?;
    let result = async move {
        let first_resolve = {
            let state = realm.lock_state().await;
            match *state {
                RealmState::New => {
                    panic!("Realm should be at least discovered")
                }
                RealmState::Discovered => true,
                RealmState::Resolved(_) => false,
                RealmState::Destroyed => {
                    return Err(ModelError::instance_not_found(realm.abs_moniker.clone()));
                }
            }
        };
        let component = realm
            .environment
            .resolve(&realm.component_url)
            .await
            .map_err(|err| ModelError::from(err))?;
        let component = Component::try_from(component)?;
        if first_resolve {
            let mut state = realm.lock_state().await;
            match *state {
                RealmState::Resolved(_) => {
                    panic!("Realm was marked Resolved during Resolve action?");
                }
                RealmState::Destroyed => {
                    return Err(ModelError::instance_not_found(realm.abs_moniker.clone()));
                }
                RealmState::New | RealmState::Discovered => {}
            }
            state.set(
                RealmState::Resolved(ResolvedRealmState::new(realm, component.decl.clone()).await),
            );
        }
        Ok((component, first_resolve))
    }
    .await;

    match result {
        Ok((component, false)) => Ok(component),
        Ok((component, true)) => {
            let event =
                Event::new(realm, Ok(EventPayload::Resolved { decl: component.decl.clone() }));
            realm.hooks.dispatch(&event).await?;
            Ok(component)
        }
        Err(e) => {
            let event = Event::new(realm, Err(EventError::new(&e, EventErrorPayload::Resolved)));
            realm.hooks.dispatch(&event).await?;
            Err(e)
        }
    }
}

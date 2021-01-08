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
    let first_resolve = {
        let state = realm.lock_state().await;
        match *state {
            RealmState::Resolved(_) => false,
            _ => true,
        }
    };
    let result = async move {
        let component = realm
            .environment
            .resolve(&realm.component_url)
            .await
            .map_err(|err| ModelError::from(err))?;
        let component = Component::try_from(component)?;
        Ok(component)
    }
    .await;

    // If a `RealmState` was installed in this call, first dispatch `Resolved` for the component's
    // realm and then dispatch `Discovered` for every static child that was discovered in the
    // manifest.
    match (first_resolve, result) {
        (false, Ok(component)) => Ok(component),
        (true, Ok(component)) => {
            {
                let mut state = realm.lock_state().await;
                match *state {
                    RealmState::Resolved(_) => {
                        panic!("Realm state was unexpectedly populated");
                    }
                    _ => {}
                }
                *state = RealmState::Resolved(
                    ResolvedRealmState::new(realm, component.decl.clone()).await,
                );
            }
            let event =
                Event::new(realm, Ok(EventPayload::Resolved { decl: component.decl.clone() }));
            realm.hooks.dispatch(&event).await?;
            Ok(component)
        }
        (_, Err(e)) => {
            let event = Event::new(realm, Err(EventError::new(&e, EventErrorPayload::Resolved)));
            realm.hooks.dispatch(&event).await?;
            Err(e)
        }
    }
}

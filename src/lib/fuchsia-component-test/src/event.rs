// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Construct component realms by listing the components and the routes between them

use {
    crate::{error::*, Event, Moniker, RealmBuilder, RouteEndpoint},
    cm_rust,
};

/// A capability that is routed through the custom realms
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Capability {
    Event(Event, cm_rust::EventMode),
}

impl Capability {
    pub fn event(event: Event, mode: cm_rust::EventMode) -> Self {
        Self::Event(event, mode)
    }
}

/// A capability route from one source component to one or more target components.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityRoute {
    pub capability: Capability,
    pub source: RouteEndpoint,
    pub targets: Vec<RouteEndpoint>,
}

/// Adds a route for an event capability between two points in the realm.
pub(crate) async fn add_event_route(
    realm: &RealmBuilder,
    route: CapabilityRoute,
) -> Result<(), Error> {
    if let RouteEndpoint::Component(moniker) = &route.source {
        if !realm.contains(moniker.clone()).await? {
            return Err(EventError::MissingRouteSource(moniker.as_str().into()).into());
        }
    }
    if route.targets.is_empty() {
        return Err(EventError::EmptyRouteTargets.into());
    }
    for target in &route.targets {
        if &route.source == target {
            return Err(EventError::RouteSourceAndTargetMatch(route.clone()).into());
        }
        if let RouteEndpoint::Component(moniker) = target {
            if !realm.contains(moniker.clone()).await? {
                return Err(EventError::MissingRouteTarget(moniker.as_str().into()).into());
            }
        }
    }

    for target in &route.targets {
        if *target == RouteEndpoint::AboveRoot {
            return add_event_route_to_above_root(realm, route).await;
        } else if route.source == RouteEndpoint::AboveRoot {
            let target = target.clone();
            return add_event_route_from_above_root(realm, route, target).await;
        } else {
            let target = target.clone();
            return add_event_route_between_components(realm, route, target).await;
        }
    }
    Ok(())
}

async fn add_event_route_to_above_root(
    _realm: &RealmBuilder,
    _route: CapabilityRoute,
) -> Result<(), Error> {
    return Err(EventError::EventsCannotBeExposed.into());
}

impl RealmBuilder {
    // Returns whether or not the target can be mutated, or if realm builder is incapable of modifying
    // it (the latter happens when something is behind a `ChildDecl`).
    async fn is_mutable(&self, moniker: Moniker) -> Result<bool, Error> {
        // We need to check if the target is mutable or immutable. We could just see if
        // `get_decl` succeeds, but if it's immutable then the server will log that an
        // error occurred. Get the parent decl and check its ChildDecls, we can't mutate this
        // node if it's behind a child decl.
        Ok(moniker.is_root()
            || !self
                .get_decl(moniker.parent().unwrap())
                .await?
                .children
                .iter()
                .any(|c| &c.name == moniker.child_name().unwrap()))
    }
}

async fn add_event_route_from_above_root(
    realm: &RealmBuilder,
    route: CapabilityRoute,
    target: RouteEndpoint,
) -> Result<(), Error> {
    // We're routing a capability from above our constructed realm to a component
    // within it
    let target_moniker = target.unwrap_component_moniker();

    if realm.contains(target_moniker.clone()).await? {
        // We need to check if the target is mutable or immutable. We could just see if
        // `get_decl` succeeds, but if it's immutable then the server will log that an
        // error occurred. Get the parent decl and check its ChildDecls, we can't mutate this
        // node if it's behind a child decl.
        if realm.is_mutable(target_moniker.clone()).await? {
            let mut target_decl = realm.get_decl(target_moniker.clone()).await?;
            add_use_for_capability(&mut target_decl.uses, &route.capability);
            realm.set_decl(target_moniker.clone(), target_decl).await?;
        }
    }

    let mut current_ancestor = target_moniker;
    while !current_ancestor.is_root() {
        let child_name = current_ancestor.child_name().unwrap().clone();
        current_ancestor = current_ancestor.parent().unwrap();

        let mut decl = realm.get_decl(current_ancestor.clone()).await?;
        add_offer_for_capability(
            &mut decl.offers,
            &route,
            cm_rust::OfferSource::Parent,
            &child_name,
            &current_ancestor,
        )?;
        realm.set_decl(current_ancestor.clone(), decl).await?;
    }
    Ok(())
}

async fn add_event_route_between_components(
    realm: &RealmBuilder,
    route: CapabilityRoute,
    target: RouteEndpoint,
) -> Result<(), Error> {
    // We're routing a capability from one component within our constructed realm to
    // another
    let source_moniker = route.source.unwrap_component_moniker();
    let target_moniker = target.unwrap_component_moniker();

    if !source_moniker.is_ancestor_of(&target_moniker) {
        return Err(EventError::EventsCannotBeExposed.into());
    }

    let mut current_ancestor = source_moniker.clone();
    for offer_child_name in source_moniker.downward_path_to(&target_moniker) {
        let mut decl = realm.get_decl(current_ancestor.clone()).await?;
        add_offer_for_capability(
            &mut decl.offers,
            &route,
            cm_rust::OfferSource::Framework,
            &offer_child_name,
            &current_ancestor,
        )?;
        realm.set_decl(current_ancestor.clone(), decl).await?;
        current_ancestor = current_ancestor.child(offer_child_name);
    }

    if realm.contains(target_moniker.clone()).await? {
        // We need to check if the target is mutable or immutable. We could just see if
        // `get_decl` succeeds, but if it's immutable then the server will log that an
        // error occurred. Get the parent decl and check its ChildDecls, we can't mutate this
        // node if it's behind a child decl.
        if realm.is_mutable(target_moniker.clone()).await? {
            let mut target_decl = realm.get_decl(target_moniker.clone()).await?;
            add_use_for_capability(&mut target_decl.uses, &route.capability);
            realm.set_decl(target_moniker.clone(), target_decl).await?;
        }
    }
    Ok(())
}

fn add_use_for_capability(uses: &mut Vec<cm_rust::UseDecl>, capability: &Capability) {
    let use_decl = match capability {
        Capability::Event(event, mode) => cm_rust::UseDecl::Event(cm_rust::UseEventDecl {
            source: cm_rust::UseSource::Parent,
            source_name: event.name().into(),
            target_name: event.name().into(),
            filter: event.filter(),
            mode: mode.clone(),
            dependency_type: cm_rust::DependencyType::Strong,
        }),
    };
    if !uses.contains(&use_decl) {
        uses.push(use_decl);
    }
}

fn add_offer_for_capability(
    offers: &mut Vec<cm_rust::OfferDecl>,
    route: &CapabilityRoute,
    offer_source: cm_rust::OfferSource,
    target_name: &str,
    moniker: &Moniker,
) -> Result<(), Error> {
    let offer_target = cm_rust::OfferTarget::static_child(target_name.to_string());
    match &route.capability {
        Capability::Event(event, mode) => {
            for offer in offers.iter() {
                if let cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                    source,
                    target_name,
                    target,
                    ..
                }) = offer
                {
                    if event.name() == target_name.str() && *target == offer_target {
                        if *source != offer_source {
                            return Err(EventError::ConflictingOffers(
                                route.clone(),
                                moniker.clone(),
                                target.clone(),
                                format!("{:?}", source),
                            )
                            .into());
                        } else {
                            // The offer we want already exists
                            return Ok(());
                        }
                    }
                }
            }
            offers.push(cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                source: offer_source,
                source_name: event.name().into(),
                target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                target_name: event.name().into(),
                filter: event.filter(),
                mode: mode.clone(),
            }));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{error, mock, ChildProperties, RouteBuilder},
        cm_rust::*,
        fidl_fuchsia_data as fdata,
        maplit::hashmap,
        std::convert::TryInto,
    };

    async fn build_and_check_results(
        builder: RealmBuilder,
        expected_results: Vec<(&'static str, ComponentDecl)>,
    ) {
        assert!(!expected_results.is_empty(), "can't build an empty realm");

        for (component, local_decl) in expected_results {
            let mut remote_decl =
                builder.get_decl(component).await.expect("component is missing from realm");

            // The assigned mock IDs may not be stable across test runs, so reset them all to 0 before
            // we compare against expected results.
            if let Some(program) = &mut remote_decl.program {
                if let Some(entries) = &mut program.info.entries {
                    for entry in entries.iter_mut() {
                        if entry.key == crate::mock::MOCK_ID_KEY {
                            entry.value =
                                Some(Box::new(fdata::DictionaryValue::Str("0".to_string())));
                        }
                    }
                }
            }

            assert_eq!(
                remote_decl, local_decl,
                "decl in realm doesn't match expectations for component  {:?}",
                component
            );
        }
    }

    #[fuchsia::test]
    async fn expose_event_from_child_error() {
        let builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder.add_child("a", "fuchsia-pkg://a", ChildProperties::new()).await.unwrap();
        let res = builder
            .add_route(
                RouteBuilder::event(Event::Started, cm_rust::EventMode::Async)
                    .source(RouteEndpoint::component("a"))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Event(error::EventError::EventsCannotBeExposed)) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn offer_event_from_child_error() {
        let builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_child("a", "fuchsia-pkg://a", ChildProperties::new())
            .await
            .unwrap()
            .add_child("b", "fuchsia-pkg://b", ChildProperties::new())
            .await
            .unwrap();
        let res = builder
            .add_route(
                RouteBuilder::event(Event::Started, cm_rust::EventMode::Async)
                    .source(RouteEndpoint::component("a"))
                    .targets(vec![RouteEndpoint::component("b")]),
            )
            .await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Event(error::EventError::EventsCannotBeExposed)) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn verify_events_routing() {
        let builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");
        builder
            .add_mock_child(
                "a",
                |_: mock::MockHandles| Box::pin(async move { Ok(()) }),
                ChildProperties::new(),
            )
            .await
            .unwrap()
            .add_mock_child(
                "a/b",
                |_: mock::MockHandles| Box::pin(async move { Ok(()) }),
                ChildProperties::new(),
            )
            .await
            .unwrap()
            .add_route(
                RouteBuilder::event(Event::Started, cm_rust::EventMode::Sync)
                    .source(RouteEndpoint::AboveRoot)
                    .targets(vec![RouteEndpoint::component("a")]),
            )
            .await
            .unwrap()
            .add_route(
                RouteBuilder::event(
                    Event::directory_ready("diagnostics"),
                    cm_rust::EventMode::Async,
                )
                .source(RouteEndpoint::component("a"))
                .targets(vec![RouteEndpoint::component("a/b")]),
            )
            .await
            .unwrap()
            .add_route(
                RouteBuilder::event(
                    Event::capability_requested("fuchsia.logger.LogSink"),
                    cm_rust::EventMode::Async,
                )
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("a/b")]),
            )
            .await
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "started".into(),
                                target: cm_rust::OfferTarget::static_child("a".to_string()),
                                target_name: "started".into(),
                                mode: EventMode::Sync,
                                filter: None,
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "capability_requested".into(),
                                target: cm_rust::OfferTarget::static_child("a".to_string()),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                            }),
                        ],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the realm builder
                            // server, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![UseDecl::Event(UseEventDecl {
                            source: UseSource::Parent,
                            source_name: "started".into(),
                            target_name: "started".into(),
                            filter: None,
                            mode: EventMode::Sync,
                            dependency_type: DependencyType::Strong,
                        })],
                        offers: vec![
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Framework,
                                source_name: "directory_ready".into(),
                                target: cm_rust::OfferTarget::static_child("b".to_string()),
                                target_name: "directory_ready".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "diagnostics".to_string()))),
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "capability_requested".into(),
                                target: cm_rust::OfferTarget::static_child("b".to_string()),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                            }),
                        ],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the realm builder
                            // server, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a/b",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Parent,
                                source_name: "directory_ready".into(),
                                target_name: "directory_ready".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "diagnostics".to_string()))),
                                dependency_type: DependencyType::Strong,
                            }),
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Parent,
                                source_name: "capability_requested".into(),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                                dependency_type: DependencyType::Strong,
                            }),
                        ],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fuchsia::test]
    async fn events_routing_doesnt_add_duplicate_use() {
        let builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");
        builder
            .add_mock_child(
                "a",
                |_: mock::MockHandles| Box::pin(async move { Ok(()) }),
                ChildProperties::new(),
            )
            .await
            .unwrap()
            .add_route(
                RouteBuilder::event(Event::Started, cm_rust::EventMode::Sync)
                    .source(RouteEndpoint::AboveRoot)
                    .targets(vec![RouteEndpoint::component("a")]),
            )
            .await
            .unwrap()
            .add_route(
                RouteBuilder::event(Event::Started, cm_rust::EventMode::Sync)
                    .source(RouteEndpoint::AboveRoot)
                    .targets(vec![RouteEndpoint::component("a")]),
            )
            .await
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![OfferDecl::Event(OfferEventDecl {
                            source: cm_rust::OfferSource::Parent,
                            source_name: "started".into(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "started".into(),
                            mode: EventMode::Sync,
                            filter: None,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the realm builder
                            // server, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![UseDecl::Event(UseEventDecl {
                            source: UseSource::Parent,
                            source_name: "started".into(),
                            target_name: "started".into(),
                            filter: None,
                            mode: EventMode::Sync,
                            dependency_type: DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the realm builder
                            // server, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }
}

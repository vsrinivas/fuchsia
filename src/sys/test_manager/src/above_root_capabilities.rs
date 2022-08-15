// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{TEST_ROOT_REALM_NAME, TEST_TYPE_REALM_MAP},
    anyhow::Error,
    fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_test::Capability as RBCapability,
    fuchsia_component_test::{
        error::Error as RealmBuilderError, Capability, RealmBuilder, Ref, Route, SubRealmBuilder,
    },
    fuchsia_fs,
    std::collections::HashMap,
};

pub struct AboveRootCapabilitiesForTest {
    capabilities: HashMap<&'static str, Vec<RBCapability>>,
}

impl AboveRootCapabilitiesForTest {
    pub async fn new(manifest_name: &str) -> Result<Self, Error> {
        let path = format!("/pkg/meta/{}", manifest_name);
        let file_proxy =
            fuchsia_fs::file::open_in_namespace(&path, fuchsia_fs::OpenFlags::RIGHT_READABLE)?;
        let component_decl = fuchsia_fs::read_file_fidl::<fdecl::Component>(&file_proxy).await?;
        let capabilities = Self::load(component_decl);
        Ok(Self { capabilities })
    }

    #[cfg(test)]
    pub fn new_empty_for_tests() -> Self {
        let empty_capabilities = HashMap::new();
        Self { capabilities: empty_capabilities }
    }

    pub async fn apply(
        &self,
        collection: &str,
        builder: &RealmBuilder,
        wrapper_realm: &SubRealmBuilder,
    ) -> Result<(), RealmBuilderError> {
        if self.capabilities.contains_key(collection) {
            for capability in &self.capabilities[collection] {
                builder
                    .add_route(
                        Route::new()
                            .capability(capability.clone())
                            .from(Ref::parent())
                            .to(wrapper_realm),
                    )
                    .await?;
                wrapper_realm
                    .add_route(
                        Route::new()
                            .capability(capability.clone())
                            .from(Ref::parent())
                            .to(Ref::child(TEST_ROOT_REALM_NAME)),
                    )
                    .await?;
            }
        }
        Ok(())
    }

    fn load(decl: fdecl::Component) -> HashMap<&'static str, Vec<RBCapability>> {
        let mut capabilities: HashMap<_, _> =
            TEST_TYPE_REALM_MAP.values().map(|v| (*v, vec![])).collect();
        for offer_decl in decl.offers.unwrap_or(vec![]) {
            match offer_decl {
                fdecl::Offer::Protocol(fdecl::OfferProtocol {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    target_name: Some(target_name),
                    ..
                }) if capabilities.contains_key(name.as_str())
                    && target_name != "fuchsia.logger.LogSink" =>
                {
                    capabilities
                        .get_mut(name.as_str())
                        .unwrap()
                        .push(Capability::protocol_by_name(target_name).into());
                }
                fdecl::Offer::Directory(fdecl::OfferDirectory {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    target_name: Some(target_name),
                    ..
                }) if capabilities.contains_key(name.as_str()) => {
                    capabilities
                        .get_mut(name.as_str())
                        .unwrap()
                        .push(Capability::directory(target_name).into());
                }
                fdecl::Offer::Storage(fdecl::OfferStorage {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    target_name: Some(target_name),
                    ..
                }) if capabilities.contains_key(name.as_str()) => {
                    let use_path = format!("/{}", target_name);
                    capabilities
                        .get_mut(name.as_str())
                        .unwrap()
                        .push(Capability::storage(target_name).path(use_path).into());
                }
                fdecl::Offer::Service(fdecl::OfferService {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    ..
                })
                | fdecl::Offer::Runner(fdecl::OfferRunner {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    ..
                })
                | fdecl::Offer::Resolver(fdecl::OfferResolver {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    ..
                }) if capabilities.contains_key(name.as_str()) => {
                    unimplemented!(
                        "Services, runners and resolvers are not supported by realm builder"
                    );
                }
                fdecl::Offer::Event(fdecl::OfferEvent {
                    target: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name })),
                    ..
                }) if capabilities.contains_key(name.as_str()) => {
                    unreachable!("No events should be routed from above root to a test.");
                }
                _ => {
                    // Ignore anything else that is not routed to test collections
                }
            }
        }
        capabilities
    }
}

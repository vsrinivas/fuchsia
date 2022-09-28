// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{warnings::Warning, SYSTEM_TEST_SHARD};
use anyhow::{bail, Context, Error};
use once_cell::sync::Lazy;
use serde::Deserialize;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CmxFacets {
    #[serde(rename = "fuchsia.test")]
    test: Option<TestFacets>,

    #[serde(rename = "fuchsia.module")]
    #[allow(unused)] // included for confidence in completeness of cmx parser but unsupported
    module: Option<CmxModule>,
}

impl CmxFacets {
    /// Convert the facets into the appropriate shards and `use`s in CML, returning the set of
    /// protocols received from children.
    pub fn convert(
        &self,
        include: &mut BTreeSet<String>,
        uses: &mut Vec<cml::Use>,
        children: &mut Vec<cml::Child>,
        warnings: &mut BTreeSet<Warning>,
    ) -> Result<BTreeSet<String>, Error> {
        let mut injected = BTreeSet::new();
        if let Some(test) = &self.test {
            injected = test.convert(include, uses, children, warnings)?;
        }

        if self.module.is_some() {
            bail!("fuchsia modules are not supported by this converter");
        }

        Ok(injected)
    }
}

#[derive(Debug, Deserialize)]
#[allow(unused)] // included for completeness of parser on in-tree cmx files but unsupported
#[serde(deny_unknown_fields)]
struct CmxModule {
    #[serde(rename = "@version")]
    version: u8,
    intent_filters: Vec<serde_json::Value>,
    suggestion_headline: String,
    composition_pattern: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct TestFacets {
    #[serde(rename = "injected-services")]
    injected: Option<BTreeMap<String, InjectedService>>,
    #[serde(rename = "system-services")]
    system: Option<Vec<String>>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields, untagged)]
enum InjectedService {
    Url(String),
    UrlWithArgs(Vec<String>),
}

impl TestFacets {
    /// Convert test facets, returning the set of all protocols that come from injected children.
    pub fn convert(
        &self,
        include: &mut BTreeSet<String>,
        uses: &mut Vec<cml::Use>,
        children: &mut Vec<cml::Child>,
        warnings: &mut BTreeSet<Warning>,
    ) -> Result<BTreeSet<String>, Error> {
        let mut injected_protocols = BTreeSet::new();
        if let Some(injected) = &self.injected {
            // for each injected service, add a child and use the protocol from it
            // do this in a separate loop from modifying cml so that we can group things
            let mut children_by_name: BTreeMap<_, BTreeSet<_>> = BTreeMap::new();
            for (protocol, v1_provider) in injected {
                if let InjectedService::UrlWithArgs(..) = v1_provider {
                    bail!("injected services that pass arguments are not supported");
                }
                let (url, InjectedServiceProvider { name, gn_target }) =
                    if let Some(p) = InjectedServicesMap::provider(protocol)? {
                        p
                    } else {
                        // this protocol doesn't need a static child in v2, get it from parent
                        // v1 protocols needed to be be in sandbox already, so just skip this one
                        // before we remove it from existing uses from parent
                        continue;
                    };

                children_by_name.entry((name, url, gn_target)).or_default().insert(protocol);
                injected_protocols.insert(protocol.to_owned());
            }

            for ((name, url, gn_target), protocols) in children_by_name {
                let child_name =
                    cml::Name::try_new(&name).with_context(|| format!("declaring child {name}"))?;
                let url =
                    cml::Url::new(&url).with_context(|| format!("parsing {url} as a CML URL"))?;

                warnings.insert(Warning::ChildNeedsGnTargetAndRouting {
                    child: name.clone(),
                    gn_target,
                });
                children.push(cml::Child {
                    name: child_name.clone(),
                    url,
                    startup: cml::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                });

                let protocols = protocols
                    .into_iter()
                    .map(|p| {
                        cml::Name::try_new(p)
                            .with_context(|| format!("defining name for use decl for {p}"))
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                uses.push(cml::Use {
                    protocol: Some(cml::OneOrMany::Many(protocols)),
                    from: Some(cml::UseFromRef::Named(child_name)),
                    ..Default::default()
                });
            }
        }

        if self.system.is_some() {
            // system tests get a different test realm, spell this as a shard
            include.insert(SYSTEM_TEST_SHARD.to_string());

            // in v1, system services need to be spelled twice, so we already have a `use` for them
        }

        Ok(injected_protocols)
    }
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InjectedServicesMap {
    protocols: BTreeMap<String, String>,
    components: BTreeMap<String, InjectedServiceProvider>,
}

static INJECTED_SERVICES_MAP: Lazy<InjectedServicesMap> = Lazy::new(|| {
    static MAP_RAW: &str = include_str!("../injected_services_map.json5");
    let parsed: InjectedServicesMap =
        serde_json5::from_str(MAP_RAW).expect("injected services map must be valid json");
    parsed
});

impl InjectedServicesMap {
    /// Returns the URL and provider info for a given protocol's injected service provider if
    /// available. Returns `None` if the protocol should be retrieved from `parent` in v2 tests.
    pub fn provider(protocol: &str) -> Result<Option<(String, InjectedServiceProvider)>, Error> {
        if let Some(url) = INJECTED_SERVICES_MAP.protocols.get(protocol) {
            if url == "parent" {
                return Ok(None);
            }
            if let Some(provider) = INJECTED_SERVICES_MAP.components.get(url) {
                Ok(Some((url.to_owned(), provider.to_owned())))
            } else {
                bail!("`{protocol}`'s provider {url} does not have a registered implementation");
            }
        } else {
            bail!("`{protocol}` does not have a registered provider for injection");
        }
    }
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InjectedServiceProvider {
    /// the name of the static child for the provider
    pub name: String,

    /// the GN target that must be added to a package to have access to the provider
    pub gn_target: String,
}

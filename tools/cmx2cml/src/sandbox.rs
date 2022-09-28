// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    features::CmxFeature, warnings::Warning, PROTOCOLS_FOR_HERMETIC_TESTS,
    PROTOCOLS_FOR_SYSTEM_TESTS, SYSTEM_TEST_SHARD,
};
use anyhow::{bail, Context, Error};
use serde::Deserialize;
use std::collections::BTreeSet;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CmxSandbox {
    services: Option<Vec<String>>,
    dev: Option<Vec<String>>,
    features: Option<Vec<CmxFeature>>,

    // unsupported
    boot: Option<Vec<String>>,
    pkgfs: Option<Vec<String>>,
    system: Option<Vec<String>>,
}

impl CmxSandbox {
    pub fn uses(
        &self,
        warnings: &mut BTreeSet<Warning>,
        include: &mut BTreeSet<String>,
        injected_protocols: &BTreeSet<String>,
        is_test: bool,
    ) -> Result<Vec<cml::Use>, Error> {
        if self.boot.is_some() {
            bail!("`sandbox.boot` key is not supported for automatic conversion");
        }
        if self.pkgfs.is_some() {
            bail!("`sandbox.pkgfs` key is not supported for automatic conversion");
        }
        if self.system.is_some() {
            bail!("`sandbox.system` key is not supported for automatic conversion");
        }

        // accumulate protocols-from-parent separately from general uses so we can merge protocols
        let mut uses = vec![];
        let mut protocols_from_parent = vec![];

        // add everything from sandbox.services to use-from-parent except for protocols that came
        // from injected services, since in v1 all protocols come from the component's parent
        // (often the sys realm)
        if let Some(services) = &self.services {
            for protocol in services {
                if injected_protocols.contains(&*protocol) {
                    continue;
                }

                let available_for_hermetic = PROTOCOLS_FOR_HERMETIC_TESTS.contains(&&**protocol);
                let available_for_system = PROTOCOLS_FOR_SYSTEM_TESTS.contains(&&**protocol);

                // if the protocol isn't available to hermetic tests, run as system test
                if is_test && !available_for_hermetic {
                    include.insert(SYSTEM_TEST_SHARD.to_string());
                }

                // if the protocol isn't available to system tests, warn the user
                if is_test && !available_for_hermetic && !available_for_system {
                    warnings.insert(Warning::TestWithUnavailableProtocol(protocol.clone()));
                }

                protocols_from_parent.push(
                    cml::Name::try_new(protocol)
                        .with_context(|| format!("parsing {protocol} into a cml name"))?,
                );
            }
        }

        if let Some(devices) = &self.dev {
            warnings.insert(Warning::DeviceDirectoryBestEffort);
            if is_test {
                // system components will get device directories from their parent but we need to
                // ask to be run in the system test realm to get them in tests
                include.insert(SYSTEM_TEST_SHARD.to_string());
            }

            for device in devices {
                // these strings are in the form `class/CLASS`, we want a directory named
                // `dev-CLASS` mounted at a component's `/dev/class/CLASS` path
                let (_class_literal, device_class) = device.split_once("/").with_context(|| {
                    format!("supported devices are under /dev/class/*, got `/dev/{}`", device)
                })?;
                uses.push(cml::Use {
                    directory: Some(
                        cml::Name::try_new(format!("dev-{}", device_class))
                            .with_context(|| format!("creating a `use` name for {device_class}"))?,
                    ),
                    rights: Some(cml::Rights(vec![cml::Right::ReadAlias])),
                    path: Some(
                        cml::Path::new(format!("/dev/{}", device))
                            .with_context(|| format!("creating a `use` path for {device_class}"))?,
                    ),
                    ..Default::default()
                });
            }
        }

        if let Some(features) = &self.features {
            for feature in features {
                let (use_decl, warning, shard) = feature.uses(is_test)?;
                if let Some(u) = use_decl {
                    if u.protocol.is_some() && u.from.is_none() {
                        match u.protocol.expect("protocol is_some was just true") {
                            cml::OneOrMany::One(p) => protocols_from_parent.push(p),
                            cml::OneOrMany::Many(_) => {
                                panic!("features should only produce a single protocol to use")
                            }
                        }
                    } else {
                        uses.push(u);
                    }
                }
                if let Some(w) = warning {
                    warnings.insert(w);
                }
                if let Some(s) = shard {
                    include.insert(s);
                }
            }
        }

        // merge all protocol uses from parent into a single use with multiple protocols, put it 1st
        if !protocols_from_parent.is_empty() {
            let other_uses = uses;
            uses = vec![cml::Use {
                protocol: Some(cml::OneOrMany::Many(protocols_from_parent)),
                ..Default::default()
            }];
            uses.extend(other_uses.into_iter());
        }

        Ok(uses)
    }
}

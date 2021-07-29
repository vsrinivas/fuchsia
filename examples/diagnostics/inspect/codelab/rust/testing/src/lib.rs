// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_examples_inspect::{ReverserMarker, ReverserProxy};
use fuchsia_component_test::{
    builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
    RealmInstance,
};

const FIZZBUZZ_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/inspect_rust_codelab_integration_tests#meta/fizzbuzz.cm";

pub struct TestOptions {
    pub include_fizzbuzz: bool,
}

impl Default for TestOptions {
    fn default() -> Self {
        TestOptions { include_fizzbuzz: true }
    }
}

pub struct IntegrationTest {
    instance: RealmInstance,
}

impl IntegrationTest {
    pub async fn start(part: usize, options: TestOptions) -> Result<Self, Error> {
        let mut builder = RealmBuilder::new().await?;
        if options.include_fizzbuzz {
            builder
                .add_component("fizzbuzz", ComponentSource::url(FIZZBUZZ_URL))
                .await?
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fuchsia.examples.inspect.FizzBuzz"),
                    source: RouteEndpoint::component("fizzbuzz"),
                    targets: vec![RouteEndpoint::component("reverser")],
                })?
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fuchsia.logger.LogSink"),
                    source: RouteEndpoint::AboveRoot,
                    targets: vec![RouteEndpoint::component("fizzbuzz")],
                })?;
        }
        builder
            .add_component(
                "reverser",
                ComponentSource::url(format!(
                    "fuchsia-pkg://fuchsia.com/inspect_rust_codelab_integration_tests#meta/part_{}.cm",
                    part
                )),
            )
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.examples.inspect.Reverser"),
                source: RouteEndpoint::component("reverser"),
                targets: vec![RouteEndpoint::AboveRoot],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![
                    RouteEndpoint::component("reverser"),
                ],
            })?;
        let instance = builder.build().create().await?;
        Ok(Self { instance })
    }

    pub fn connect_to_reverser(&self) -> Result<ReverserProxy, Error> {
        self.instance.root.connect_to_protocol_at_exposed_dir::<ReverserMarker>()
    }

    pub fn reverser_moniker_for_selectors(&self) -> String {
        format!("fuchsia_component_test_collection\\:{}/reverser", self.instance.root.child_name())
    }
}

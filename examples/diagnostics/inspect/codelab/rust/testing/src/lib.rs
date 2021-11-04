// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_examples_inspect::{ReverserMarker, ReverserProxy};
use fuchsia_component_test::{
    ChildProperties, RealmBuilder, RealmInstance, RouteBuilder, RouteEndpoint,
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
        let builder = RealmBuilder::new().await?;
        builder
            .add_child(
                "reverser",
                format!(
                    "fuchsia-pkg://fuchsia.com/inspect_rust_codelab_integration_tests#meta/part_{}.cm",
                    part
                ),
                ChildProperties::new(),
            )
            .await?;
        if options.include_fizzbuzz {
            builder
                .add_child("fizzbuzz", FIZZBUZZ_URL, ChildProperties::new())
                .await?
                .add_route(
                    RouteBuilder::protocol("fuchsia.examples.inspect.FizzBuzz")
                        .source(RouteEndpoint::component("fizzbuzz"))
                        .targets(vec![RouteEndpoint::component("reverser")]),
                )
                .await?
                .add_route(
                    RouteBuilder::protocol("fuchsia.logger.LogSink")
                        .source(RouteEndpoint::AboveRoot)
                        .targets(vec![RouteEndpoint::component("fizzbuzz")]),
                )
                .await?;
        }
        builder
            .add_route(
                RouteBuilder::protocol_marker::<ReverserMarker>()
                    .source(RouteEndpoint::component("reverser"))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol("fuchsia.logger.LogSink")
                    .source(RouteEndpoint::AboveRoot)
                    .targets(vec![RouteEndpoint::component("reverser")]),
            )
            .await?;
        let instance = builder.build().await?;
        Ok(Self { instance })
    }

    pub fn connect_to_reverser(&self) -> Result<ReverserProxy, Error> {
        self.instance.root.connect_to_protocol_at_exposed_dir::<ReverserMarker>()
    }

    pub fn reverser_moniker_for_selectors(&self) -> String {
        format!("fuchsia_component_test_collection\\:{}/reverser", self.instance.root.child_name())
    }
}

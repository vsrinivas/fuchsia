// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_examples_inspect::{ReverserMarker, ReverserProxy};
use fuchsia_component_test::new::{
    Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
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
        let reverser = builder
            .add_child(
                "reverser",
                format!(
                    "fuchsia-pkg://fuchsia.com/inspect_rust_codelab_integration_tests#meta/part_{}.cm",
                    part
                ),
                ChildOptions::new(),
            )
            .await?;
        if options.include_fizzbuzz {
            let fizzbuzz = builder.add_child("fizzbuzz", FIZZBUZZ_URL, ChildOptions::new()).await?;
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(
                            "fuchsia.examples.inspect.FizzBuzz",
                        ))
                        .from(&fizzbuzz)
                        .to(&reverser),
                )
                .await?;
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                        .from(Ref::parent())
                        .to(&fizzbuzz),
                )
                .await?;
        }
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<ReverserMarker>())
                    .from(&reverser)
                    .to(Ref::parent()),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&reverser),
            )
            .await?;
        let instance = builder.build().await?;
        Ok(Self { instance })
    }

    pub fn connect_to_reverser(&self) -> Result<ReverserProxy, Error> {
        self.instance.root.connect_to_protocol_at_exposed_dir::<ReverserMarker>()
    }

    pub fn reverser_moniker_for_selectors(&self) -> String {
        format!("realm_builder\\:{}/reverser", self.instance.root.child_name())
    }
}

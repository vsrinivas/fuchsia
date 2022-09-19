// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fidl_examples_routing_echo::*;
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route};

const COMPONENT_URL: &str = "fuchsia-pkg://fuchsia.com/echo_server_test#meta/echo_server.cm";

pub struct EchoServerTest;
impl EchoServerTest {
    pub async fn create_realm() -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        let echo_server =
            builder.add_child("echo_server", COMPONENT_URL, ChildOptions::new()).await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&echo_server),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                    .from(&echo_server)
                    .to(Ref::parent()),
            )
            .await?;

        let instance = builder.build().await?;
        Ok(instance)
    }
    pub fn connect_to_echomarker(instance: &RealmInstance) -> EchoProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<EchoMarker>()
            .expect("connecting to Echo");
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use async_trait::async_trait;
use fuchsia_component_test::new::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};

const COMPONENT_URL: &str = "fuchsia-pkg://fuchsia.com/echo_client_test#meta/echo_client.cm";

#[async_trait]
pub trait Mocks {
    async fn service_1_impl(handles: LocalComponentHandles) -> Result<(), Error>;
}
pub struct EchoClientTest;

impl EchoClientTest {
    pub async fn create_realm() -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        let echo_client =
            builder.add_child("echo_client", COMPONENT_URL, ChildOptions::new()).await?;
        let service_1 = builder
            .add_local_child(
                "service_1",
                move |handles: LocalComponentHandles| {
                    Box::pin(EchoClientTest::service_1_impl(handles))
                },
                ChildOptions::new(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                    .from(&service_1)
                    .to(Ref::parent())
                    .to(&echo_client),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&echo_client),
            )
            .await?;

        let instance = builder.build().await?;
        Ok(instance)
    }
}

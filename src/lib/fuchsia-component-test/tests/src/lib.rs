// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    cm_rust, cm_types,
    fidl_fidl_examples_routing_echo::{self as fecho, EchoMarker as EchoClientStatsMarker},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fcdecl,
    fidl_fuchsia_data as fdata, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::{
        error::Error as RealmBuilderError,
        new::{Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route},
    },
    futures::{channel::mpsc, FutureExt, SinkExt, StreamExt, TryStreamExt},
    std::convert::TryInto,
};

const V1_ECHO_CLIENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cmx";
const V2_ECHO_CLIENT_ABSOLUTE_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cm";
const V2_ECHO_CLIENT_RELATIVE_URL: &'static str = "#meta/echo_client.cm";

const V1_ECHO_SERVER_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_server.cmx";
const V2_ECHO_SERVER_ABSOLUTE_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_server.cm";
const V2_ECHO_SERVER_RELATIVE_URL: &'static str = "#meta/echo_server.cm";

const DEFAULT_ECHO_STR: &'static str = "Hello Fuchsia!";

#[fuchsia::test]
async fn v1_component_route_to_parent() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child = builder.add_legacy_child("child", V1_ECHO_SERVER_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    Ok(())
}

#[fuchsia::test]
async fn absolute_component_route_to_parent() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child =
        builder.add_child("child", V2_ECHO_SERVER_ABSOLUTE_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    Ok(())
}

#[fuchsia::test]
async fn relative_component_route_to_parent() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child =
        builder.add_child("child", V2_ECHO_SERVER_RELATIVE_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    Ok(())
}

#[fuchsia::test]
async fn local_component_route_to_parent() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);
    let child = builder
        .add_local_child(
            "child",
            move |h| echo_server_mock(DEFAULT_ECHO_STR, send_echo_server_called.clone(), h).boxed(),
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    assert!(
        receive_echo_server_called.next().await.is_some(),
        "failed to observe the mock server report a successful connection from a client"
    );
    Ok(())
}

#[fuchsia::test]
async fn v1_component_route_to_parent_in_sub_realm() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child_realm = builder.add_child_realm("child-realm", ChildOptions::new()).await?;
    let child =
        child_realm.add_legacy_child("child", V1_ECHO_SERVER_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child_realm)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child_realm),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    Ok(())
}

#[fuchsia::test]
async fn absolute_component_route_to_parent_in_sub_realm() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child_realm = builder.add_child_realm("child-realm", ChildOptions::new()).await?;
    let child =
        child_realm.add_child("child", V2_ECHO_SERVER_ABSOLUTE_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child_realm)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child_realm),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    Ok(())
}

#[fuchsia::test]
async fn relative_component_route_to_parent_in_sub_realm() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child_realm = builder.add_child_realm("child-realm", ChildOptions::new()).await?;
    let child =
        child_realm.add_child("child", V2_ECHO_SERVER_RELATIVE_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child_realm)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child_realm),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    Ok(())
}

#[fuchsia::test]
async fn local_component_route_to_parent_in_sub_realm() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);
    let child_realm = builder.add_child_realm("child-realm", ChildOptions::new()).await?;
    let child = child_realm
        .add_local_child(
            "child",
            move |h| echo_server_mock(DEFAULT_ECHO_STR, send_echo_server_called.clone(), h).boxed(),
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child_realm)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child_realm),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child)
                .to(Ref::parent()),
        )
        .await?;
    child_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await?;
    let instance = builder.build().await?;
    let echo_proxy = instance.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(
        Some(DEFAULT_ECHO_STR.to_string()),
        echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?,
    );
    assert!(
        receive_echo_server_called.next().await.is_some(),
        "failed to observe the mock server report a successful connection from a client"
    );
    Ok(())
}

#[fuchsia::test]
async fn get_and_replace_realm_decl() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let mut root_decl = builder.get_realm_decl().await?;
    assert_eq!(root_decl, cm_rust::ComponentDecl::default());
    root_decl.children.push(cm_rust::ChildDecl {
        name: "example-child".to_string(),
        url: "example://url".to_string(),
        startup: fcdecl::StartupMode::Eager,
        on_terminate: None,
        environment: None,
    });
    builder.replace_realm_decl(root_decl.clone()).await?;
    assert_eq!(root_decl, builder.get_realm_decl().await?);
    Ok(())
}

#[fuchsia::test]
async fn get_and_replace_component_decl() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let child = builder
        .add_local_child("child", |_| futures::future::pending().boxed(), ChildOptions::new())
        .await?;
    let mut child_decl = builder.get_component_decl(&child).await?;
    child_decl.children.push(cm_rust::ChildDecl {
        name: "example-grand-child".to_string(),
        url: "example://url".to_string(),
        startup: fcdecl::StartupMode::Eager,
        on_terminate: None,
        environment: None,
    });
    builder.replace_component_decl(&child, child_decl.clone()).await?;
    assert_eq!(child_decl, builder.get_component_decl(&child).await?);
    Ok(())
}

#[fuchsia::test]
async fn protocol_with_siblings_test() -> Result<(), Error> {
    // [START mock_component_example]
    // Create a new mpsc channel for passing a message from the echo server function
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

    // Build a new realm
    let builder = RealmBuilder::new().await?;
    // Add the echo server, which is implemented by the echo_server_mock function (defined below).
    // Give this function access to the channel created above, along with the mock component's
    // handles
    let child_a = builder
        .add_local_child(
            "a",
            move |handles: LocalComponentHandles| {
                echo_server_mock(DEFAULT_ECHO_STR, send_echo_server_called.clone(), handles).boxed()
            },
            ChildOptions::new(),
        )
        .await?;
    // Add the echo client with a URL source
    let child_b = builder
        .add_child(
            "b",
            "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cm",
            ChildOptions::new().eager(),
        )
        .await?;
    // Route the fidl.examples.routing.echo.Echo protocol from a to b
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child_a)
                .to(&child_b),
        )
        .await?;
    // Route the logsink to `b`, so it can inform us of any issues
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child_b),
        )
        .await?;

    // Create and start the realm
    let _instance = builder.build().await?;

    // Wait for the channel we created above to receive a message
    assert!(receive_echo_server_called.next().await.is_some());
    // [END mock_component_example]
    Ok(())
}

#[fuchsia::test]
async fn protocol_with_uncle_test() -> Result<(), Error> {
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

    let builder = RealmBuilder::new().await?;

    let sub_realm = builder.add_child_realm("parent", ChildOptions::new().eager()).await?;

    let echo_server = builder
        .add_local_child(
            "echo-server",
            move |handles| {
                echo_server_mock(DEFAULT_ECHO_STR, send_echo_server_called.clone(), handles).boxed()
            },
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&echo_server)
                .to(&sub_realm),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server)
                .to(&sub_realm),
        )
        .await?;

    let echo_client = sub_realm
        .add_child("echo-client", V2_ECHO_CLIENT_ABSOLUTE_URL, ChildOptions::new().eager())
        .await?;
    sub_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_client),
        )
        .await?;
    let _instance = builder.build().await?;

    assert!(receive_echo_server_called.next().await.is_some());
    Ok(())
}

#[fuchsia::test]
async fn examples() -> Result<(), Error> {
    // This test exists purely to provide us with live snippets for the realm builder
    // documentation
    {
        // [START add_a_and_b_example]
        // Create a new RealmBuilder instance, which we will use to define a new realm
        let builder = RealmBuilder::new().await?;
        let child_a = builder
            // Add component `a` to the realm, which will be fetched with a URL
            .add_child("a", "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm", ChildOptions::new())
            .await?;
        // Add component `b` to the realm, which will be fetched with a URL
        let child_b = builder
            .add_child("b", "fuchsia-pkg://fuchsia.com/bar#meta/bar.cm", ChildOptions::new())
            .await?;
        // [END add_a_and_b_example]

        // [START route_from_a_to_b_example]
        // Add a new route for the protocol capability `fidl.examples.routing.echo.Echo`
        // from `a` to `b`
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fecho::EchoMarker>())
                    .from(&child_a)
                    .to(&child_b),
            )
            .await?;
        // [END route_from_a_to_b_example]
    }
    {
        let builder = RealmBuilder::new().await?;
        let child_a =
            builder.add_child("a", V2_ECHO_CLIENT_ABSOLUTE_URL, ChildOptions::new()).await?;
        let child_b =
            builder.add_child("b", V2_ECHO_CLIENT_ABSOLUTE_URL, ChildOptions::new()).await?;
        // [START route_logsink_example]
        // Routes `fuchsia.logger.LogSink` from above root to `a` and `b`
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&child_a)
                    .to(&child_b),
            )
            .await?;
        // [END route_logsink_example]
    }
    {
        let builder = RealmBuilder::new().await?;
        let child_b =
            builder.add_child("b", V2_ECHO_CLIENT_ABSOLUTE_URL, ChildOptions::new()).await?;
        // [START route_to_above_root_example]
        // Adds a route for the protocol capability
        // `fidl.examples.routing.echo.EchoClientStats` from `b` to the realm's parent
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fidl.examples.routing.echo.EchoClientStats",
                    ))
                    .from(&child_b)
                    .to(Ref::parent()),
            )
            .await?;

        // [START create_realm]
        // Creates the realm, and add it to the collection to start its execution
        let realm_instance = builder.build().await?;
        // [END create_realm]

        // [START connect_to_protocol]
        // Connects to `fidl.examples.routing.echo.EchoClientStats`, which is provided
        // by `b` in the created realm
        let echo_client_stats_proxy =
            realm_instance.root.connect_to_protocol_at_exposed_dir::<EchoClientStatsMarker>()?;
        // [END connect_to_protocol]
        // [END route_to_above_root_example]
        drop(echo_client_stats_proxy);
    }
    #[allow(unused, unused_mut)]
    {
        let builder = RealmBuilder::new().await?;
        let child_realm_a = builder.add_child_realm("a", ChildOptions::new()).await?;
        let child_b =
            child_realm_a.add_child("b", V2_ECHO_CLIENT_ABSOLUTE_URL, ChildOptions::new()).await?;

        // [START mutate_generated_manifest_example]
        let mut root_manifest = builder.get_realm_decl().await?;
        // root_manifest is mutated in whatever way is needed
        builder.replace_realm_decl(root_manifest).await?;

        let mut a_manifest = builder.get_component_decl(&child_realm_a).await.unwrap();
        // a_manifest is mutated in whatever way is needed
        builder.replace_component_decl(&child_realm_a, a_manifest).await.unwrap();
        // [END mutate_generated_manifest_example]
    }
    Ok(())
}

// This test confirms that dynamic components in the built realm can use URLs that are relative to
// the test package (this is a special case the realm builder resolver needs to handle).
#[fuchsia::test]
async fn mock_component_with_a_relative_dynamic_child() -> Result<(), Error> {
    let (send_echo_client_results, mut receive_echo_client_results) = mpsc::channel(1);

    let collection_name = "dynamic-children".to_string();
    let collection_name_for_mock = collection_name.clone();

    let builder = RealmBuilder::new().await?;
    let echo_client = builder
        .add_local_child(
            "echo-client",
            move |handles| {
                let collection_name_for_mock = collection_name_for_mock.clone();
                let mut send_echo_client_results = send_echo_client_results.clone();
                async move {
                    let realm_proxy = handles.connect_to_service::<fcomponent::RealmMarker>()?;
                    realm_proxy
                        .create_child(
                            &mut fcdecl::CollectionRef { name: collection_name_for_mock.clone() },
                            fcdecl::Child {
                                name: Some("echo-server".to_string()),
                                url: Some(V2_ECHO_SERVER_RELATIVE_URL.to_string()),
                                startup: Some(fcdecl::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..fcdecl::Child::EMPTY
                            },
                            fcomponent::CreateChildArgs::EMPTY,
                        )
                        .await?
                        .expect("failed to create child");
                    let (exposed_dir_proxy, exposed_dir_server_end) =
                        fidl::endpoints::create_proxy()?;
                    realm_proxy
                        .open_exposed_dir(
                            &mut fcdecl::ChildRef {
                                name: "echo-server".to_string(),
                                collection: Some(collection_name_for_mock.clone()),
                            },
                            exposed_dir_server_end,
                        )
                        .await?
                        .expect("failed to open exposed dir");
                    let echo_proxy = fuchsia_component::client::connect_to_protocol_at_dir_root::<
                        fecho::EchoMarker,
                    >(&exposed_dir_proxy)?;
                    let out = echo_proxy.echo_string(Some(DEFAULT_ECHO_STR)).await?;
                    if Some(DEFAULT_ECHO_STR.to_string()) != out {
                        return Err(format_err!("unexpected echo result: {:?}", out));
                    }
                    send_echo_client_results.send(()).await.expect("failed to send results");
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    let mut echo_client_decl = builder.get_component_decl(&echo_client).await?;
    echo_client_decl.collections.push(cm_rust::CollectionDecl {
        name: collection_name.clone(),
        durability: fcdecl::Durability::Transient,
        allowed_offers: cm_types::AllowedOffers::StaticOnly,
        environment: None,
    });
    echo_client_decl.capabilities.push(cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl {
        name: "fidl.examples.routing.echo.Echo".into(),
        source_path: Some("/svc/fidl.examples.routing.echo.Echo".try_into().unwrap()),
    }));
    echo_client_decl.offers.push(cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
        source: cm_rust::OfferSource::Self_,
        source_name: "fidl.examples.routing.echo.Echo".into(),
        target: cm_rust::OfferTarget::Collection(collection_name.clone()),
        target_name: "fidl.examples.routing.echo.Echo".into(),
        dependency_type: cm_rust::DependencyType::Strong,
    }));
    echo_client_decl.uses.push(cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
        source: cm_rust::UseSource::Framework,
        source_name: "fuchsia.component.Realm".into(),
        target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
        dependency_type: cm_rust::DependencyType::Strong,
    }));
    builder.replace_component_decl(&echo_client, echo_client_decl).await?;

    let _instance = builder.build().await?;

    assert!(receive_echo_client_results.next().await.is_some());
    Ok(())
}

#[fuchsia::test]
async fn altered_echo_client_args() -> Result<(), Error> {
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

    let builder = RealmBuilder::new().await?;
    let echo_client = builder
        .add_child("echo_client", V2_ECHO_CLIENT_RELATIVE_URL, ChildOptions::new().eager())
        .await?;
    let echo_server = builder
        .add_local_child(
            "echo_server",
            move |handles| {
                echo_server_mock("Whales rule!", send_echo_server_called.clone(), handles).boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&echo_server)
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

    // Change the program.args section of the manifest, to alter the string it will try to echo
    let mut echo_client_decl = builder.get_component_decl(&echo_client).await?;
    for entry in echo_client_decl.program.as_mut().unwrap().info.entries.as_mut().unwrap() {
        if entry.key.as_str() == "args" {
            entry.value =
                Some(Box::new(fdata::DictionaryValue::StrVec(vec!["Whales rule!".to_string()])));
        }
    }
    builder.replace_component_decl(&echo_client, echo_client_decl).await?;
    let _instance = builder.build().await?;

    assert!(receive_echo_server_called.next().await.is_some());

    Ok(())
}

async fn setup_echo_client_realm(builder: &RealmBuilder) -> Result<mpsc::Receiver<()>, Error> {
    let (send_echo_server_called, receive_echo_server_called) = mpsc::channel(1);
    let echo_server = builder
        .add_local_child(
            "echo-server",
            move |h| echo_server_mock(DEFAULT_ECHO_STR, send_echo_server_called.clone(), h).boxed(),
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&echo_server)
                .to(Ref::child("echo-client")),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server)
                .to(Ref::child("echo-client")),
        )
        .await?;
    Ok(receive_echo_server_called)
}

#[fuchsia::test]
async fn echo_clients() -> Result<(), Error> {
    // This test runs a series of echo clients from different sources against a mock echo server,
    // confirming that each client successfully connects to the server.

    let client_name = "echo-client";
    let child_opts = ChildOptions::new().eager();
    {
        let builder = RealmBuilder::new().await?;
        builder.add_legacy_child(client_name, V1_ECHO_CLIENT_URL, child_opts.clone()).await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        realm_instance.destroy().await?;
    }

    {
        let builder = RealmBuilder::new().await?;
        builder.add_child(client_name, V2_ECHO_CLIENT_ABSOLUTE_URL, child_opts.clone()).await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        realm_instance.destroy().await?;
    }

    {
        let builder = RealmBuilder::new().await?;
        builder.add_child(client_name, V2_ECHO_CLIENT_RELATIVE_URL, child_opts.clone()).await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        realm_instance.destroy().await?;
    }

    {
        let (send_echo_client_results, mut receive_echo_client_results) = mpsc::channel(1);

        let builder = RealmBuilder::new().await?;
        builder
            .add_local_child(
                client_name,
                move |h| echo_client_mock(send_echo_client_results.clone(), h).boxed(),
                child_opts.clone(),
            )
            .await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        assert!(
            receive_echo_client_results.next().await.is_some(),
            "failed to observe the mock client report success"
        );

        realm_instance.destroy().await?;
    }

    Ok(())
}

#[fuchsia::test]
async fn echo_clients_in_nested_component_manager() -> Result<(), Error> {
    // This test runs a series of echo clients from different sources against a mock echo server,
    // confirming that each client successfully connects to the server.

    {
        let builder = RealmBuilder::new().await?;
        builder
            .add_legacy_child("echo-client", V1_ECHO_CLIENT_URL, ChildOptions::new().eager())
            .await?;

        // assert_matches wants to pretty-print what went wrong if the assert fails, but neither
        // sub-type in the returned Result here implements Debug.
        match builder.build_in_nested_component_manager("#meta/component_manager.cm").await {
            Err(RealmBuilderError::LegacyChildrenUnsupportedInNestedComponentManager) => (),
            _ => panic!("legacy children should be unsupported in nested component managers"),
        }
    }

    {
        let builder = RealmBuilder::new().await?;
        builder
            .add_child("echo-client", V2_ECHO_CLIENT_ABSOLUTE_URL, ChildOptions::new().eager())
            .await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance =
            builder.build_in_nested_component_manager("#meta/component_manager.cm").await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        realm_instance.destroy().await?;
    }

    {
        let builder = RealmBuilder::new().await?;
        builder
            .add_child("echo-client", V2_ECHO_CLIENT_RELATIVE_URL, ChildOptions::new().eager())
            .await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance =
            builder.build_in_nested_component_manager("#meta/component_manager.cm").await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        realm_instance.destroy().await?;
    }

    {
        let (send_echo_client_results, mut receive_echo_client_results) = mpsc::channel(1);

        let builder = RealmBuilder::new().await?;
        builder
            .add_local_child(
                "echo-client",
                move |h| echo_client_mock(send_echo_client_results.clone(), h).boxed(),
                ChildOptions::new().eager(),
            )
            .await?;
        let mut receive_echo_server_called = setup_echo_client_realm(&builder).await?;
        let realm_instance =
            builder.build_in_nested_component_manager("#meta/component_manager.cm").await?;

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection",
        );

        assert!(
            receive_echo_client_results.next().await.is_some(),
            "failed to observe the mock client report success"
        );

        realm_instance.destroy().await?;
    }

    Ok(())
}

async fn setup_echo_server_realm(builder: &RealmBuilder) -> Result<mpsc::Receiver<()>, Error> {
    let (send_echo_client_results, receive_echo_client_results) = mpsc::channel(1);
    let echo_client = builder
        .add_local_child(
            "echo-client",
            move |h| echo_client_mock(send_echo_client_results.clone(), h).boxed(),
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(Ref::child("echo-server"))
                .to(&echo_client),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(Ref::child("echo-server"))
                .to(&echo_client),
        )
        .await?;
    Ok(receive_echo_client_results)
}

#[fuchsia::test]
async fn echo_servers() -> Result<(), Error> {
    let server_name = "echo-server";
    let child_opts = ChildOptions::new().eager();

    {
        let builder = RealmBuilder::new().await?;
        builder.add_legacy_child(server_name, V1_ECHO_SERVER_URL, child_opts.clone()).await?;
        let mut receive_echo_client_results = setup_echo_server_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_client_results.next().await.is_some(),
            "failed to observe the mock client report success",
        );

        realm_instance.destroy().await?;
    }

    {
        let builder = RealmBuilder::new().await?;
        builder.add_child(server_name, V2_ECHO_SERVER_ABSOLUTE_URL, child_opts.clone()).await?;
        let mut receive_echo_client_results = setup_echo_server_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_client_results.next().await.is_some(),
            "failed to observe the mock client report success",
        );

        realm_instance.destroy().await?;
    }

    {
        let builder = RealmBuilder::new().await?;
        builder.add_child(server_name, V2_ECHO_SERVER_RELATIVE_URL, child_opts.clone()).await?;
        let mut receive_echo_client_results = setup_echo_server_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_client_results.next().await.is_some(),
            "failed to observe the mock client report success",
        );

        realm_instance.destroy().await?;
    }

    {
        let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

        let builder = RealmBuilder::new().await?;
        builder
            .add_local_child(
                server_name,
                move |h| {
                    echo_server_mock(DEFAULT_ECHO_STR, send_echo_server_called.clone(), h).boxed()
                },
                child_opts.clone(),
            )
            .await?;
        let mut receive_echo_client_results = setup_echo_server_realm(&builder).await?;
        let realm_instance = builder.build().await?;

        assert!(
            receive_echo_client_results.next().await.is_some(),
            "failed to observe the mock client report success",
        );

        assert!(
            receive_echo_server_called.next().await.is_some(),
            "failed to observe the mock server report a successful connection from a client"
        );

        realm_instance.destroy().await?;
    }

    Ok(())
}

// [START echo_server_mock]
// A mock echo server implementation, that will crash if it doesn't receive anything other than the
// contents of `expected_echo_str`. It takes and sends a message over `send_echo_server_called`
// once it receives one echo request.
async fn echo_server_mock(
    expected_echo_string: &'static str,
    send_echo_server_called: mpsc::Sender<()>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    // Create a new ServiceFs to host FIDL protocols from
    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];

    // Add the echo protocol to the ServiceFs
    fs.dir("svc").add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
        let mut send_echo_server_called = send_echo_server_called.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(fecho::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.expect("failed to serve echo service")
            {
                assert_eq!(Some(expected_echo_string.to_string()), value);
                // Send the received string back to the client
                responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");

                // Use send_echo_server_called to report back that we successfully received a
                // message and it aligned with our expectations
                send_echo_server_called.send(()).await.expect("failed to send results");
            }
        }));
    });

    // Run the ServiceFs on the outgoing directory handle from the mock handles
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}
// [END echo_server_mock]

async fn echo_client_mock(
    mut send_echo_client_results: mpsc::Sender<()>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    let echo = handles.connect_to_service::<fecho::EchoMarker>()?;
    let out = echo.echo_string(Some(DEFAULT_ECHO_STR)).await?;
    if Some(DEFAULT_ECHO_STR.to_string()) != out {
        return Err(format_err!("unexpected echo result: {:?}", out));
    }
    send_echo_client_results.send(()).await.expect("failed to send results");
    Ok(())
}

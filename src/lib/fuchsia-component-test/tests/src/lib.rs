// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    assert_matches::assert_matches,
    cm_rust::{self, FidlIntoNative},
    cm_types,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_routing_echo::{self as fecho, EchoMarker as EchoClientStatsMarker},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fcdecl,
    fidl_fuchsia_component_test as ftest, fidl_fuchsia_data as fdata,
    fidl_fuchsia_examples_services as fex_services, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::EventStream2Marker,
    fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::{
        error::Error as RealmBuilderError, Capability, ChildOptions, DirectoryContents,
        LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    fuchsia_fs,
    futures::{channel::mpsc, future::pending, FutureExt, SinkExt, StreamExt, TryStreamExt},
    std::convert::TryInto,
};

const V1_ECHO_CLIENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cmx";
const V2_ECHO_CLIENT_ABSOLUTE_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cm";
const V2_ECHO_CLIENT_RELATIVE_URL: &'static str = "#meta/echo_client.cm";
const V2_ECHO_CLIENT_STRUCTURED_CONFIG_RELATIVE_URL: &'static str = "#meta/echo_client_sc.cm";

const V1_ECHO_SERVER_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_server.cmx";
const V2_ECHO_SERVER_ABSOLUTE_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_server.cm";
const V2_ECHO_SERVER_RELATIVE_URL: &'static str = "#meta/echo_server.cm";

const ECHO_REALM_RELATIVE_URL: &'static str = "#meta/echo_realm.cm";

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
async fn local_component_stop_notifier() -> Result<(), Error> {
    let (send_stop_notifier_registered, mut receive_stop_notifier_registered) = mpsc::channel(1);
    let (send_stop_notifier_called, mut receive_stop_notifier_called) = mpsc::channel(1);

    let builder = RealmBuilder::new().await?;
    let _child = builder
        .add_local_child(
            "child",
            move |handles| {
                let mut send_stop_notifier_registered = send_stop_notifier_registered.clone();
                let mut send_stop_notifier_called = send_stop_notifier_called.clone();
                async move {
                    let stop_notifier = handles.register_stop_notifier().await;
                    send_stop_notifier_registered
                        .send(())
                        .await
                        .expect("failed to send that the stop notifier was registered");
                    stop_notifier.await.expect("failed to wait for stop notification");
                    send_stop_notifier_called
                        .send(())
                        .await
                        .expect("failed to send that the stop notifier was registered");
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    let instance = builder.build().await?;
    assert!(
        receive_stop_notifier_registered.next().await.is_some(),
        "failed to observe the local component register a stop notifier"
    );
    drop(instance);
    assert!(
        receive_stop_notifier_called.next().await.is_some(),
        "failed to observe the local component receive notice with the stop notifier"
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
    let child =
        builder.add_local_child("child", |_| pending().boxed(), ChildOptions::new()).await?;
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
                    let realm_proxy = handles.connect_to_protocol::<fcomponent::RealmMarker>()?;
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
        environment: None,
        allowed_offers: cm_types::AllowedOffers::StaticOnly,
        allow_long_names: false,
        persistent_storage: None,
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
        availability: cm_rust::Availability::Required,
    }));
    echo_client_decl.uses.push(cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
        source: cm_rust::UseSource::Framework,
        source_name: "fuchsia.component.Realm".into(),
        target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
        dependency_type: cm_rust::DependencyType::Strong,
        availability: cm_rust::Availability::Required,
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

#[fuchsia::test]
async fn config_packaged_values_only() -> Result<(), Error> {
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

    let builder = RealmBuilder::new().await?;
    let echo_client = builder
        .add_child(
            "echo_client",
            V2_ECHO_CLIENT_STRUCTURED_CONFIG_RELATIVE_URL,
            ChildOptions::new().eager(),
        )
        .await?;
    let echo_server = builder
        .add_local_child(
            "echo_server",
            move |handles| {
                echo_server_mock(
                    "Hello Fuchsia!, Hi, There, false, 100",
                    send_echo_server_called.clone(),
                    handles,
                )
                .boxed()
            },
            ChildOptions::new(),
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

    let _instance = builder.build().await?;

    assert!(receive_echo_server_called.next().await.is_some());

    Ok(())
}

#[fuchsia::test]
async fn config_set_values_only() -> Result<(), Error> {
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

    let builder = RealmBuilder::new().await?;
    let echo_client = builder
        .add_child(
            "echo_client",
            V2_ECHO_CLIENT_STRUCTURED_CONFIG_RELATIVE_URL,
            ChildOptions::new().eager(),
        )
        .await?;

    let echo_server = builder
        .add_local_child(
            "echo_server",
            move |handles| {
                echo_server_mock(
                    "Foobar!, Hey, Folks, true, 42",
                    send_echo_server_called.clone(),
                    handles,
                )
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?;

    // fail to replace a config field in a component that doesn't have a config schema
    builder.init_mutable_config_to_empty(&echo_server).await.unwrap();
    assert_matches!(
        builder.set_config_value_bool(&echo_server, "echo_bool", false).await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::NoConfigSchema))
    );

    // fail to replace a config field for a component that hasn't opted in
    assert_matches!(
        builder.set_config_value_string(&echo_client, "doesnt_exist", "test").await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ConfigOverrideUnsupported))
    );

    // allow setting config values now
    builder.init_mutable_config_to_empty(&echo_client).await.unwrap();

    // fail to replace a field that doesn't exist
    assert_matches!(
        builder.set_config_value_string(&echo_client, "doesnt_exist", "test").await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::NoSuchConfigField))
    );

    // fail to replace a field with the wrong type
    assert_matches!(
        builder.set_config_value_string(&echo_client, "echo_bool", "test").await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ConfigValueInvalid))
    );

    // fail to replace a string that violates max_len
    let long_string = String::from_utf8(vec![b'F'; 20]).unwrap();
    assert_matches!(
        builder.set_config_value_string(&echo_client, "echo_string", long_string.clone()).await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ConfigValueInvalid))
    );

    // fail to replace a vector whose string element violates max_len
    assert_matches!(
        builder
            .set_config_value_string_vector(&echo_client, "echo_string_vector", vec![long_string])
            .await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ConfigValueInvalid))
    );

    // fail to replace a vector that violates max_count
    assert_matches!(
        builder
            .set_config_value_string_vector(
                &echo_client,
                "echo_string_vector",
                vec!["a", "b", "c", "d"],
            )
            .await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ConfigValueInvalid))
    );

    // succeed at replacing all fields with proper constraints
    builder.set_config_value_string(&echo_client, "echo_string", "Foobar!").await.unwrap();
    builder
        .set_config_value_string_vector(&echo_client, "echo_string_vector", ["Hey", "Folks"])
        .await
        .unwrap();
    builder.set_config_value_bool(&echo_client, "echo_bool", true).await.unwrap();
    builder.set_config_value_uint64(&echo_client, "echo_num", 42).await.unwrap();
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

    let _instance = builder.build().await?;

    assert!(receive_echo_server_called.next().await.is_some());

    Ok(())
}

#[fuchsia::test]
async fn config_mix_packaged_and_set_values() {
    let (send_echo_server_called, mut receive_echo_server_called) = mpsc::channel(1);

    let builder = RealmBuilder::new().await.unwrap();
    let echo_client = builder
        .add_child(
            "echo_client",
            V2_ECHO_CLIENT_STRUCTURED_CONFIG_RELATIVE_URL,
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();

    let echo_server = builder
        .add_local_child(
            "echo_server",
            move |handles| {
                echo_server_mock(
                    "Foobar!, Hey, Folks, false, 100",
                    send_echo_server_called.clone(),
                    handles,
                )
                .boxed()
            },
            ChildOptions::new(),
        )
        .await
        .unwrap();

    // use the packaged values for fields not set by this test
    builder.init_mutable_config_from_package(&echo_client).await.unwrap();

    // succeed at replacing two of four fields with proper constraints
    builder.set_config_value_string(&echo_client, "echo_string", "Foobar!").await.unwrap();
    builder
        .set_config_value_string_vector(&echo_client, "echo_string_vector", ["Hey", "Folks"])
        .await
        .unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&echo_server)
                .to(&echo_client),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_client),
        )
        .await
        .unwrap();

    let _instance = builder.build().await.unwrap();

    assert!(receive_echo_server_called.next().await.is_some());
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

#[fuchsia::test]
async fn route_required_fields_for_local_component() {
    // This test confirms that certain fields are required when routing capabilities to or from a
    // local component

    // Given a source, a target, and a flag signifying if either the source or a target are a local
    // component, checks if the server will return an error for various scenarios in which a flag
    // required when interfacing with local components is omitted.
    async fn check_required_status(from: Ref, to: Ref, extra_fields_are_required: bool) {
        let builder = RealmBuilder::new().await.unwrap();
        builder.add_child("non_local", "test://a", ChildOptions::new()).await.unwrap();
        builder
            .add_local_child("local_1", |_| pending().boxed(), ChildOptions::new())
            .await
            .unwrap();
        builder
            .add_local_child("local_2", |_| pending().boxed(), ChildOptions::new())
            .await
            .unwrap();

        // Confirms that the result returned by the server matches our expectations
        let assert_add_route_results = |results| match (extra_fields_are_required, results) {
            (
                true,
                Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::CapabilityInvalid)),
            ) => (),
            (false, Ok(())) => (),
            (true, Ok(())) => {
                panic!("the server didn't return an error when a required field was missing")
            }
            (false, Err(e)) => {
                panic!("the server returned an error when it should have succeeded: {:?}", e)
            }
            (true, Err(e)) => panic!("we were expecting an error, but not this one: {:?}", e),
        };

        // Routing a directory without the path specified
        let res = builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("2").rights(fio::RX_STAR_DIR))
                    .from(from.clone())
                    .to(to.clone()),
            )
            .await;
        assert_add_route_results(res);

        // Routing a directory without the rights specified
        let res = builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("2").path("/2"))
                    .from(from.clone())
                    .to(to.clone()),
            )
            .await;
        assert_add_route_results(res);

        // Routing storage without the path specified
        if to != Ref::parent() {
            // Storage capabilities cannot be exposed
            let res = builder
                .add_route(
                    Route::new()
                        .capability(Capability::storage("data"))
                        .from(from.clone())
                        .to(to.clone()),
                )
                .await;
            assert_add_route_results(res);
        }
    }

    let non_local_ref = Ref::child("non_local");
    let local_1_ref = Ref::child("local_1");
    let local_2_ref = Ref::child("local_2");

    // These (from,to) tuples are scenarios where the additional, local-component-specific fields
    // are _not_ required.
    let fields_not_required_combinations =
        vec![(non_local_ref.clone(), Ref::parent()), (Ref::parent(), non_local_ref.clone())];
    // These (from,to) tuples are scenarios where the additional, local-component-specific fields
    // _are_ required.
    let fields_required_combinations = vec![
        (Ref::parent(), local_1_ref.clone()),
        (local_1_ref.clone(), non_local_ref.clone()),
        (non_local_ref.clone(), local_1_ref.clone()),
        (local_1_ref.clone(), local_2_ref.clone()),
        (local_2_ref.clone(), Ref::parent()),
    ];

    for (from, to) in fields_not_required_combinations {
        check_required_status(from, to, false).await;
    }
    for (from, to) in fields_required_combinations {
        check_required_status(from, to, true).await;
    }
}

#[fuchsia::test]
async fn event_streams_test() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let (tx, mut rx) = crate::mpsc::unbounded();
    // Root service that will serves directories and services.
    let root = builder
        .add_local_child(
            "root",
            move |handles| {
                let _ = &handles;
                // Block forever
                async move {
                    futures::future::pending::<()>().await;
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    let listener = builder
        .add_local_child(
            "listener",
            move |handles| {
                let tx = tx.clone();
                async move {
                    let (proxy, event_stream_server) =
                        fidl::endpoints::create_proxy::<EventStream2Marker>().unwrap();
                    let events_dir = handles.clone_from_namespace("events").unwrap();
                    events_dir.open(
                        fio::OpenFlags::RIGHT_READABLE,
                        fio::MODE_TYPE_SERVICE,
                        "event_stream",
                        ServerEnd::new(event_stream_server.into_channel()),
                    )?;
                    proxy.get_next().await.unwrap();
                    tx.unbounded_send(())?;
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::service::<fex_services::BankAccountMarker>())
                .from(&root)
                .to(Ref::parent()),
        )
        .await?;
    // Add event streams from parent
    builder
        .add_route(
            Route::new()
                .capability(Capability::event_stream("started_v2").path("/events/event_stream"))
                .from(Ref::parent())
                .to(&listener),
        )
        .await?;
    let _instance = builder.build().await?;
    rx.next().await.unwrap();
    Ok(())
}

#[fuchsia::test]
async fn route_storage() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let (send_storage_used, mut receive_storage_used) = mpsc::channel(1);
    let storage_user = builder
        .add_local_child(
            "storage_user",
            move |handles| {
                let mut send_storage_used = send_storage_used.clone();
                async move {
                    let data_dir = handles.clone_from_namespace("data")?;
                    let example_file = fuchsia_fs::directory::open_file(
                        &data_dir,
                        "example_file",
                        fio::OpenFlags::RIGHT_READABLE
                            | fio::OpenFlags::RIGHT_WRITABLE
                            | fio::OpenFlags::CREATE,
                    )
                    .await
                    .expect("failed to open example_file");
                    let example_data = "example data";
                    fuchsia_fs::write_file(&example_file, example_data).await?;
                    let _: Result<u64, i32> = example_file.seek(fio::SeekOrigin::Start, 0).await?;
                    let file_contents = fuchsia_fs::read_file(&example_file).await?;
                    assert_eq!(example_data, file_contents.as_str());
                    send_storage_used.send(()).await.expect("failed to send results");
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::storage("data").path("/data"))
                .from(Ref::parent())
                .to(&storage_user),
        )
        .await?;
    let _realm_instance = builder.build().await?;
    assert!(
        receive_storage_used.next().await.is_some(),
        "failed to observe the local component report a successful usage of its storage"
    );
    Ok(())
}

#[fuchsia::test]
async fn route_service() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    let service_provider = builder
        .add_local_child(
            "service_provider",
            move |handles| {
                async move {
                    let _ = &handles;
                    let mut fs = fserver::ServiceFs::new();
                    fs.dir("svc").add_unified_service(|req: fex_services::BankAccountRequest| req);
                    fs.serve_connection(handles.outgoing_dir)?;
                    fs.for_each_concurrent(None, move |request| async move {
                        match request {
                            fex_services::BankAccountRequest::ReadOnly(mut stream) => {
                                while let Some(request) = stream
                                    .try_next()
                                    .await
                                    .expect("failed to get next read-only request")
                                {
                                    match request {
                                        fex_services::ReadOnlyAccountRequest::GetOwner {
                                            responder,
                                        } => {
                                            responder
                                                .send("hippos")
                                                .expect("failed to send service response");
                                        }
                                        _ => panic!("unexpected request"),
                                    }
                                }
                            }
                            _ => panic!("unexpected request"),
                        }
                    })
                    .await;
                    Err(format_err!("should not have exited on its own"))
                }
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?;
    let (send_service_used, mut receive_service_used) = mpsc::channel(1);
    let service_user = builder
        .add_local_child(
            "service_user",
            move |handles| {
                let mut send_service_used = send_service_used.clone();
                async move {
                    let read_only_account = handles
                        .connect_to_service::<fex_services::BankAccountMarker>()?
                        .read_only()?;
                    let owner = read_only_account.get_owner().await?;
                    assert_eq!("hippos", owner.as_str());
                    send_service_used.send(()).await.expect("failed to send results");
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::service::<fex_services::BankAccountMarker>())
                .from(&service_provider)
                .to(&service_user),
        )
        .await?;
    let _realm_instance = builder.build().await?;
    assert!(
        receive_service_used.next().await.is_some(),
        "failed to observe the local component report a successful usage of its service"
    );
    Ok(())
}

#[fuchsia::test]
async fn fail_to_set_invalid_decls() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;

    let child_a = builder.add_child("a", "test://a", ChildOptions::new()).await?;
    let child_b = builder.add_local_child("b", |_| pending().boxed(), ChildOptions::new()).await?;

    // We cannot replace the decl for a child added with an absolute URL
    assert_matches!(
        builder.replace_component_decl(&child_a, cm_rust::ComponentDecl::default()).await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ChildDeclNotVisible))
    );

    // We cannot replace the decl for a local component with one missing the realm-builder
    // specifics in the program section
    assert_matches!(
        builder.replace_component_decl(&child_b, cm_rust::ComponentDecl::default()).await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::ImmutableProgram))
    );

    // We cannot replace the decl for a component with an invalid decl (references a non-existent
    // child)
    assert_matches!(
        builder
            .replace_component_decl(
                &child_b,
                cm_rust::ComponentDecl {
                    exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                        source: cm_rust::ExposeSource::Child("does-not-exist".to_string()),
                        source_name: "fuchsia.examples.Echo".into(),
                        target: cm_rust::ExposeTarget::Parent,
                        target_name: "fuchsia.examples.Echo".into(),
                    })],
                    ..cm_rust::ComponentDecl::default()
                }
            )
            .await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::InvalidComponentDecl))
    );

    // We cannot replace the realm's decl with an invalid decl (references a non-existent child)
    assert_matches!(
        builder
            .replace_realm_decl(cm_rust::ComponentDecl {
                exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                    source: cm_rust::ExposeSource::Child("does-not-exist".to_string()),
                    source_name: "fuchsia.examples.Echo".into(),
                    target: cm_rust::ExposeTarget::Parent,
                    target_name: "fuchsia.examples.Echo".into(),
                })],
                ..cm_rust::ComponentDecl::default()
            })
            .await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::InvalidComponentDecl))
    );

    // We _can_ replace the realm's decl with a decl that references children added with the
    // 'add_child' calls, and thus don't yet have a valid ChildDecl in the manifest.
    let mut realm_decl = builder.get_realm_decl().await?;
    realm_decl.exposes.push(cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: cm_rust::ExposeSource::Child("b".to_string()),
        source_name: "fuchsia.examples.Echo".into(),
        target: cm_rust::ExposeTarget::Parent,
        target_name: "fuchsia.examples.Echo".into(),
    }));
    assert_matches!(builder.replace_realm_decl(realm_decl).await, Ok(()));

    Ok(())
}

#[fuchsia::test]
async fn read_only_directory() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;

    let local_component_impl = |mut send_file_contents: mpsc::Sender<String>,
                                handles: LocalComponentHandles| async move {
        let config_dir = handles.clone_from_namespace("config").expect("failed to open /config");
        let config_file = fuchsia_fs::directory::open_file(
            &config_dir,
            "config.txt",
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .await
        .expect("failed to open config.txt");
        let config_file_contents =
            fuchsia_fs::read_file(&config_file).await.expect("failed to read config.txt");
        send_file_contents
            .send(config_file_contents)
            .await
            .expect("failed to send config.txt contents");
        Ok(())
    };

    let (send_a_file_contents, mut receive_a_file_contents) = mpsc::channel(1);
    let child_a = builder
        .add_local_child(
            "a",
            move |handles| local_component_impl(send_a_file_contents.clone(), handles).boxed(),
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .read_only_directory(
            "config",
            vec![&child_a],
            DirectoryContents::new().add_file("config.txt", "a"),
        )
        .await
        .unwrap();

    let (send_b_file_contents, mut receive_b_file_contents) = mpsc::channel(1);
    let child_b = builder
        .add_local_child(
            "b",
            move |handles| local_component_impl(send_b_file_contents.clone(), handles).boxed(),
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .read_only_directory(
            "config",
            vec![(&child_b).into(), Ref::parent()],
            DirectoryContents::new().add_file("config.txt", "b"),
        )
        .await
        .unwrap();

    let instance = builder.build().await?;

    assert_eq!(receive_a_file_contents.next().await, Some("a".to_string()),);
    assert_eq!(receive_b_file_contents.next().await, Some("b".to_string()),);

    let exposed_dir = instance.root.get_exposed_dir();
    let config_file = fuchsia_fs::directory::open_file(
        &exposed_dir,
        "config/config.txt",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .expect("failed to open config.txt");
    let config_file_contents =
        fuchsia_fs::read_file(&config_file).await.expect("failed to read config.txt");
    assert_eq!("b".to_string(), config_file_contents);

    Ok(())
}

#[fuchsia::test]
async fn from_relative_url() -> Result<(), Error> {
    let builder = RealmBuilder::from_relative_url(ECHO_REALM_RELATIVE_URL).await?;

    let echo_client_decl_file = fuchsia_fs::file::open_in_namespace(
        "/pkg/meta/echo_client.cm",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )?;
    let echo_client_decl: fcdecl::Component =
        fuchsia_fs::read_file_fidl(&echo_client_decl_file).await?;

    assert_eq!(
        builder.get_component_decl("echo_client").await?,
        echo_client_decl.fidl_into_native()
    );

    let echo_server_decl_file = fuchsia_fs::file::open_in_namespace(
        "/pkg/meta/echo_server.cm",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )?;
    let echo_server_decl: fcdecl::Component =
        fuchsia_fs::read_file_fidl(&echo_server_decl_file).await?;

    assert_eq!(
        builder.get_component_decl("echo_server").await?,
        echo_server_decl.fidl_into_native()
    );

    let echo_realm_decl_file = fuchsia_fs::file::open_in_namespace(
        "/pkg/meta/echo_realm.cm",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )?;
    let mut echo_realm_decl: fcdecl::Component =
        fuchsia_fs::read_file_fidl(&echo_realm_decl_file).await?;

    // The realm builder server removes these decls so it can manage them itself
    echo_realm_decl.children = Some(vec![]);

    assert_eq!(builder.get_realm_decl().await?, echo_realm_decl.fidl_into_native());

    Ok(())
}

#[fuchsia::test]
async fn from_relative_url_invalid_manifest() -> Result<(), Error> {
    // The file referenced here is intentionally not a component manifest
    assert_matches!(
        RealmBuilder::from_relative_url("#data/component_manager_realm_builder_config").await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::DeclReadError))
    );

    assert_matches!(
        RealmBuilder::from_relative_url("#meta/does-not-exist.cm").await,
        Err(RealmBuilderError::ServerError(ftest::RealmBuilderError::DeclNotFound))
    );

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
    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    Ok(())
}
// [END echo_server_mock]

async fn echo_client_mock(
    mut send_echo_client_results: mpsc::Sender<()>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    let echo = handles.connect_to_protocol::<fecho::EchoMarker>()?;
    let out = echo.echo_string(Some(DEFAULT_ECHO_STR)).await?;
    if Some(DEFAULT_ECHO_STR.to_string()) != out {
        return Err(format_err!("unexpected echo result: {:?}", out));
    }
    send_echo_client_results.send(()).await.expect("failed to send results");
    Ok(())
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    assert_matches::assert_matches,
    cm_rust::{self, OfferDeclCommon},
    cm_types,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
    },
    fuchsia_fs,
    futures::{channel::mpsc, FutureExt, SinkExt, StreamExt, TryStreamExt},
};

#[fuchsia::test]
async fn routing_succeeds_with_dynamic_offer() {
    let (_realm_instance, _local_components_task, mut success_receiver) =
        new_realm(vec![fdecl::Offer::Protocol(fdecl::OfferProtocol {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: "child_a".to_string(),
                collection: None,
            })),
            source_name: Some(fecho::EchoMarker::PROTOCOL_NAME.to_string()),
            target_name: Some(fecho::EchoMarker::PROTOCOL_NAME.to_string()),
            dependency_type: Some(fdecl::DependencyType::Strong),
            availability: Some(fdecl::Availability::Required),
            // It is important that `target` is not set
            ..fdecl::OfferProtocol::EMPTY
        })])
        .await;

    // Now let's create the realm that we'll dynamically create inside of the parent realm
    assert_matches!(
        success_receiver.next().await,
        Some(true),
        "failed to receive success signal from local component"
    );
}

#[fuchsia::test]
async fn routing_fails_without_dynamic_offer() {
    let (_realm_instance, _local_components_task, mut success_receiver) = new_realm(vec![]).await;

    // Now let's create the realm that we'll dynamically create inside of the parent realm
    assert_matches!(
        success_receiver.next().await,
        Some(false),
        "local component claims it succeeded when it should not have"
    );
}

async fn new_realm(
    dynamic_offers: Vec<fdecl::Offer>,
) -> (RealmInstance, fasync::Task<()>, mpsc::Receiver<bool>) {
    let (success_sender, success_receiver) = mpsc::channel(1);

    // Create a child realm that holds an echo client. We're going to create this dynamically
    // ourselves, so that we can set dynamic offers when its created.
    let builder = RealmBuilder::new().await.unwrap();
    let child_b = builder
        .add_local_child(
            "child_b",
            move |h| echo_client_mock(h, success_sender.clone()).boxed(),
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(Ref::parent())
                .to(&child_b),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::protocol::<fcomponent::BinderMarker>()
                        // We want the realm_user to be able to bind directly to this child, but
                        // realm builder will "helpfully" add an expose for
                        // `fuchsia.component.Binder` to the realm. We'll need to rename this route
                        // so as to not clash with the implicit one.
                        .as_("fuchsia.component.Binder2"),
                )
                .from(&child_b)
                .to(Ref::parent()),
        )
        .await
        .unwrap();
    let (child_realm_url, child_b_execution) = builder.initialize().await.unwrap();

    // Now create the realm that has an echo server, a collection, and a local component that will
    // exercise the realm protocol for us (since getting protocols across a nested component
    // manager boundary is complicated).
    let builder = RealmBuilder::new().await.unwrap();
    let child_a = builder
        .add_local_child("child_a", move |h| echo_server_mock(h).boxed(), ChildOptions::new())
        .await
        .unwrap();

    let realm_user = builder
        .add_local_child(
            "realm_user",
            move |h| realm_user(h, child_realm_url.clone(), dynamic_offers.clone()).boxed(),
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fcomponent::RealmMarker>())
                .from(Ref::framework())
                .to(&realm_user),
        )
        .await
        .unwrap();

    // We want child_a to declare and expose this capability, but we want to offer it to a dynamic
    // child using dynamic offers. Let's accomplish this by routing it to the collection with realm
    // builder but then manually deleting the offer that gets added in the realm decl.
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fecho::EchoMarker>())
                .from(&child_a)
                .to(Ref::collection("dynamic_children")),
        )
        .await
        .unwrap();
    let mut realm_decl = builder.get_realm_decl().await.unwrap();
    realm_decl.collections.push(cm_rust::CollectionDecl {
        name: "dynamic_children".to_string(),
        durability: fdecl::Durability::Transient,
        environment: None,
        allowed_offers: cm_types::AllowedOffers::StaticAndDynamic,
        allow_long_names: false,
        persistent_storage: None,
    });
    realm_decl.offers = realm_decl
        .offers
        .into_iter()
        .filter(|o| o.target() != &cm_rust::OfferTarget::Collection("dynamic_children".to_string()))
        .collect();
    builder.replace_realm_decl(realm_decl).await.unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fcomponent::RealmMarker>())
                .from(Ref::framework())
                .to(Ref::parent()),
        )
        .await
        .unwrap();
    (
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap(),
        child_b_execution,
        success_receiver,
    )
}

// Uses the fuchsia.component.Realm protocol to create a new dynamic child with a dynamic offer
// from `child_a` for the echo protocol, and binds to the new child.
async fn realm_user(
    handles: LocalComponentHandles,
    child_realm_url: String,
    dynamic_offers: Vec<fdecl::Offer>,
) -> Result<(), Error> {
    let realm_proxy = handles.connect_to_protocol::<fcomponent::RealmMarker>().unwrap();
    let mut collection_ref = fdecl::CollectionRef { name: "dynamic_children".to_string() };
    realm_proxy
        .create_child(
            &mut collection_ref,
            fdecl::Child {
                name: Some("child_realm".to_string()),
                url: Some(child_realm_url),
                startup: Some(fdecl::StartupMode::Lazy),
                ..fdecl::Child::EMPTY
            },
            fcomponent::CreateChildArgs {
                dynamic_offers: Some(dynamic_offers),
                ..fcomponent::CreateChildArgs::EMPTY
            },
        )
        .await
        .expect("FIDL error for create child")
        .expect("component manager error for create child");
    let mut child_ref = fdecl::ChildRef {
        name: "child_realm".to_string(),
        collection: Some("dynamic_children".to_string()),
    };
    let (exposed_proxy, exposed_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    realm_proxy
        .open_exposed_dir(&mut child_ref, exposed_server_end)
        .await
        .expect("FIDL error for open exposed directory")
        .expect("component manager error for open exposed directory");
    let _binder_node = fuchsia_fs::directory::open_node_no_describe(
        &exposed_proxy,
        "fuchsia.component.Binder2",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_SERVICE,
    )
    .unwrap();

    Ok(())
}

async fn echo_server_mock(handles: LocalComponentHandles) -> Result<(), Error> {
    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
        tasks.push(fasync::Task::local(async move {
            while let Some(fecho::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.expect("failed to serve echo service")
            {
                responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
            }
        }));
    });

    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    Ok(())
}

async fn echo_client_mock(
    handles: LocalComponentHandles,
    mut send_echo_client_results: mpsc::Sender<bool>,
) -> Result<(), Error> {
    let echo_str = "Hello, hippos!";
    let echo = handles.connect_to_protocol::<fecho::EchoMarker>()?;
    match echo.echo_string(Some(echo_str)).await {
        Ok(response) if response == Some(echo_str.to_string()) => {
            send_echo_client_results.send(true).await.expect("failed to send results");
        }
        _ => {
            send_echo_client_results.send(false).await.expect("failed to send results");
        }
    }
    Ok(())
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    cm_rust, fidl, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_resolution as fresolution, fidl_fuchsia_mem as fmem,
    fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::new::{ChildOptions, LocalComponentHandles, RealmBuilder},
    futures::{channel::mpsc, FutureExt, SinkExt, StreamExt, TryStreamExt},
    std::convert::TryInto,
};

const CAST_URL: &'static str = "cast:00000000";

#[fuchsia::test]
async fn resolve_cast_url() {
    let (success_sender, mut success_receiver) = mpsc::channel(1);
    let builder = RealmBuilder::new().await.unwrap();
    let cast_resolver = builder
        .add_local_child(
            "cast_resolver",
            move |h| resolver_component(h, success_sender.clone()).boxed(),
            ChildOptions::new(),
        )
        .await
        .unwrap();
    let mut cast_resolver_decl = builder.get_component_decl(&cast_resolver).await.unwrap();
    cast_resolver_decl.capabilities.push(cm_rust::CapabilityDecl::Resolver(
        cm_rust::ResolverDecl {
            name: "cast_resolver".try_into().unwrap(),
            source_path: Some("/svc/fuchsia.component.resolution.Resolver".try_into().unwrap()),
        },
    ));
    cast_resolver_decl.exposes.push(cm_rust::ExposeDecl::Resolver(cm_rust::ExposeResolverDecl {
        source: cm_rust::ExposeSource::Self_,
        source_name: "cast_resolver".try_into().unwrap(),
        target: cm_rust::ExposeTarget::Parent,
        target_name: "cast_resolver".try_into().unwrap(),
    }));
    builder.replace_component_decl(&cast_resolver, cast_resolver_decl).await.unwrap();
    let mut realm_decl = builder.get_realm_decl().await.unwrap();
    realm_decl.environments.push(cm_rust::EnvironmentDecl {
        name: "cast_env".to_string(),
        extends: fdecl::EnvironmentExtends::Realm,
        runners: vec![],
        resolvers: vec![cm_rust::ResolverRegistration {
            resolver: "cast_resolver".try_into().unwrap(),
            source: cm_rust::RegistrationSource::Child("cast_resolver".to_string()),
            scheme: "cast".to_string(),
        }],
        debug_capabilities: vec![],
        stop_timeout_ms: None,
    });
    builder.replace_realm_decl(realm_decl).await.unwrap();
    builder
        .add_child("cast_app", CAST_URL, ChildOptions::new().environment("cast_env").eager())
        .await
        .unwrap();

    let _instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();

    assert!(
        success_receiver.next().await.is_some(),
        "failed to receive success signal from local component"
    );
}

async fn resolver_component(
    handles: LocalComponentHandles,
    success_sender: mpsc::Sender<()>,
) -> Result<(), Error> {
    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |mut stream: fresolution::ResolverRequestStream| {
        let mut success_sender = success_sender.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(req) = stream.try_next().await.expect("failed to serve resolver") {
                match req {
                    fresolution::ResolverRequest::Resolve { component_url, responder } => {
                        assert_eq!(component_url, CAST_URL);
                        responder.send(&mut Ok(new_fresolution_component())).unwrap();
                        success_sender.send(()).await.expect("failed to send results");
                    }
                    fresolution::ResolverRequest::ResolveWithContext {
                        component_url,
                        context: _,
                        responder,
                    } => {
                        assert_eq!(component_url, CAST_URL);
                        responder.send(&mut Ok(new_fresolution_component())).unwrap();
                        success_sender.send(()).await.expect("failed to send results");
                    }
                }
            }
        }));
    });

    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    Ok(())
}

fn new_fresolution_component() -> fresolution::Component {
    fresolution::Component {
        url: Some(CAST_URL.to_string()),
        decl: Some(fmem::Data::Bytes(
            fidl::encoding::encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fdecl::Component::EMPTY.clone(),
            )
            .unwrap(),
        )),
        ..fresolution::Component::EMPTY
    }
}

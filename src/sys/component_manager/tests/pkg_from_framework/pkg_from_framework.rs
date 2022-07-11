// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_io as fio,
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    fuchsia_fs,
    futures::{channel::mpsc, FutureExt, SinkExt, StreamExt},
    std::convert::TryInto,
};

// The path in our namespace we can read the example configuration from, which is used to compare
// against the values read from the routed pkg dir.
const EXAMPLE_CONFIG_PATH: &'static str = "/pkg/data/example_config";
const EXAMPLE_CONFIG_FILENAME: &'static str = "example_config";

fn get_expected_config_contents() -> String {
    std::fs::read_to_string(EXAMPLE_CONFIG_PATH)
        .expect("failed to read example config from test namespace")
}

async fn read_example_config_and_assert_contents(
    handles: LocalComponentHandles,
    mut success_sender: mpsc::Sender<()>,
) -> Result<(), Error> {
    let config_dir =
        handles.clone_from_namespace("config").expect("failed to clone config from namespace");
    let example_config_file = fuchsia_fs::directory::open_file(
        &config_dir,
        EXAMPLE_CONFIG_FILENAME,
        fio::OpenFlags::RIGHT_READABLE,
    )
    .await
    .expect("failed to open example config file");
    let example_config_contents = fuchsia_fs::read_file(&example_config_file)
        .await
        .expect("failed to read example config file");
    assert_eq!(example_config_contents, get_expected_config_contents());
    success_sender.send(()).await.expect("failed to send success");
    Ok(())
}

#[fuchsia::test]
async fn offer_pkg_from_framework() {
    let (success_sender, mut success_receiver) = mpsc::channel(1);

    let builder = RealmBuilder::new().await.unwrap();
    let config_reader = builder
        .add_local_child(
            "config-reader",
            move |h| read_example_config_and_assert_contents(h, success_sender.clone()).boxed(),
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("pkg")
                        .as_("config")
                        .subdir("data")
                        .path("/config")
                        .rights(fio::R_STAR_DIR),
                )
                .from(Ref::framework())
                .to(&config_reader),
        )
        .await
        .unwrap();
    let _instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();

    assert!(
        success_receiver.next().await.is_some(),
        "failed to receive success signal from local component"
    );
}

#[fuchsia::test]
async fn expose_pkg_from_framework() {
    let (success_sender, mut success_receiver) = mpsc::channel(1);

    let builder = RealmBuilder::new().await.unwrap();
    let config_reader = builder
        .add_local_child(
            "config-reader",
            move |h| read_example_config_and_assert_contents(h, success_sender.clone()).boxed(),
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();
    let config_provider = builder
        .add_child_from_decl(
            "config-provider",
            cm_rust::ComponentDecl {
                exposes: vec![cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                    source: cm_rust::ExposeSource::Framework,
                    source_name: "pkg".into(),
                    target: cm_rust::ExposeTarget::Parent,
                    target_name: "config".into(),
                    rights: Some(fio::R_STAR_DIR),
                    subdir: Some("data".into()),
                })],
                ..cm_rust::ComponentDecl::default()
            },
            ChildOptions::new(),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::directory("config").path("/config").rights(fio::R_STAR_DIR))
                .from(&config_provider)
                .to(&config_reader),
        )
        .await
        .unwrap();
    let _instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();

    assert!(
        success_receiver.next().await.is_some(),
        "failed to receive success signal from local component"
    );
}

#[fuchsia::test]
async fn use_pkg_from_framework() {
    let (success_sender, mut success_receiver) = mpsc::channel(1);

    let builder = RealmBuilder::new().await.unwrap();
    let config_reader = builder
        .add_local_child(
            "config-reader",
            move |h| read_example_config_and_assert_contents(h, success_sender.clone()).boxed(),
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();
    let mut config_reader_decl = builder.get_component_decl(&config_reader).await.unwrap();
    config_reader_decl.uses.push(cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
        source: cm_rust::UseSource::Framework,
        source_name: "pkg".into(),
        target_path: "/config".try_into().unwrap(),
        rights: fio::R_STAR_DIR,
        subdir: Some("data".into()),
        dependency_type: cm_rust::DependencyType::Strong,
        availability: cm_rust::Availability::Required,
    }));
    builder.replace_component_decl(&config_reader, config_reader_decl).await.unwrap();
    let _instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();

    assert!(
        success_receiver.next().await.is_some(),
        "failed to receive success signal from local component"
    );
}

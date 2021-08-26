// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_hub::io::Directory,
    component_hub::{list, select, show},
    moniker::AbsoluteMonikerBase,
    std::path::PathBuf,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn list() {
    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let component = list::Component::parse("test".to_string(), hub_dir).await.unwrap();

    assert!(!component.is_cmx);
    assert!(component.is_running);
    assert_eq!(component.name, "test");
    assert_eq!(component.children.len(), 1);

    let child = component.children.get(0).unwrap();
    assert_eq!(child.name, "foo");
    assert!(!child.is_running);
    assert!(!child.is_cmx);
    assert!(child.children.is_empty());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn show() {
    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let components = show::find_components("test.cm".to_string(), hub_dir).await.unwrap();

    assert_eq!(components.len(), 1);
    let component = &components[0];

    // The test runner may include a specific hash in the component URL
    assert!(component.url.starts_with("fuchsia-pkg://fuchsia.com/component_hub_integration_test"));
    assert!(component.url.ends_with("#meta/test.cm"));

    assert!(component.moniker.is_root());
    assert_eq!(component.component_type, "CML static component");

    assert!(component.resolved.is_some());
    let resolved = component.resolved.as_ref().unwrap();

    let incoming_capabilities = &resolved.incoming_capabilities;
    assert_eq!(incoming_capabilities.len(), 2);

    let incoming_capability = &incoming_capabilities[0];
    assert_eq!(incoming_capability, "fuchsia.logger.LogSink");

    let incoming_capability = &incoming_capabilities[1];
    assert_eq!(incoming_capability, "hub");

    // We do not verify the contents of the execution, because they are largely dependent on
    // the Rust Test Runner
    assert!(component.execution.is_some());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn select() {
    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let mut components =
        select::find_components("fuchsia.foo.Bar".to_string(), hub_dir).await.unwrap();

    assert_eq!(components.len(), 1);
    let component = components.remove(0);
    assert!(component.is_root());

    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let mut components = select::find_components("minfs".to_string(), hub_dir).await.unwrap();

    assert_eq!(components.len(), 1);
    let component = components.remove(0);
    assert!(component.is_root());
}

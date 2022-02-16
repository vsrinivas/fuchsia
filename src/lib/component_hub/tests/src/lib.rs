// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_hub::io::Directory,
    component_hub::{list, select, show},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
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
    let hub_dir = Directory::from_namespace(&hub_path).unwrap();

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
    assert_eq!(incoming_capabilities.len(), 3);

    let incoming_capability = &incoming_capabilities[0];
    assert_eq!(incoming_capability, "fuchsia.foo.Bar");

    let incoming_capability = &incoming_capabilities[1];
    assert_eq!(incoming_capability, "fuchsia.logger.LogSink");

    let incoming_capability = &incoming_capabilities[2];
    assert_eq!(incoming_capability, "hub");

    assert_eq!(resolved.config.len(), 2);
    let field1 = &resolved.config[0];
    let field2 = &resolved.config[1];
    assert_eq!(field1.key, "my_string");
    assert_eq!(field1.value, "\"hello, world!\"");
    assert_eq!(field2.key, "my_uint8");
    assert_eq!(field2.value, "255");

    // We do not verify the contents of the execution, because they are largely dependent on
    // the Rust Test Runner
    assert!(component.execution.is_some());

    let hub_dir = Directory::from_namespace(&hub_path).unwrap();
    let components = show::find_components("foo.cm".to_string(), hub_dir).await.unwrap();
    assert_eq!(components.len(), 1);
    let component = &components[0];
    assert_eq!(component.moniker, AbsoluteMoniker::parse_str("/foo").unwrap());
    assert_eq!(component.url, "#meta/foo.cm");
    assert_eq!(component.component_type, "CML static component");
    assert!(component.resolved.is_none());
    assert!(component.execution.is_none());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn select() {
    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let select::MatchingComponents { mut exposed, mut used } =
        select::find_components("fuchsia.foo.Bar".to_string(), hub_dir).await.unwrap();

    assert_eq!(exposed.len(), 1);
    assert_eq!(used.len(), 1);
    let exposed_component = exposed.remove(0);
    let used_component = used.remove(0);
    assert!(exposed_component.is_root());
    assert!(used_component.is_root());

    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let select::MatchingComponents { mut exposed, used } =
        select::find_components("minfs".to_string(), hub_dir).await.unwrap();

    assert_eq!(exposed.len(), 1);
    assert_eq!(used.len(), 0);
    let exposed_component = exposed.remove(0);
    assert!(exposed_component.is_root());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn clone() {
    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();
    assert!(hub_dir.clone().is_ok());
}

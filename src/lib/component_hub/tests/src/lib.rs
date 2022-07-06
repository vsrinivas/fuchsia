// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_hub::io::Directory,
    component_hub::{list, select, show},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    std::path::PathBuf,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn list() {
    let explorer = connect_to_protocol::<fsys::RealmExplorerMarker>().unwrap();
    let query = connect_to_protocol::<fsys::RealmQueryMarker>().unwrap();

    let mut instances = list::get_all_instances(&explorer, &query, None).await.unwrap();

    assert_eq!(instances.len(), 3);

    let instance = instances.remove(0);
    assert_eq!(instance.state, list::InstanceState::Started);
    assert_eq!(instance.moniker, AbsoluteMoniker::root());
    assert!(instance.url.unwrap().ends_with("#meta/test.cm"));
    assert!(!instance.is_cmx);

    let instance = instances.remove(0);
    assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/echo_server").unwrap());
    assert_eq!(instance.url.unwrap(), "#meta/echo_server.cm");
    assert!(!instance.is_cmx);

    let instance = instances.remove(0);
    assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/foo").unwrap());
    assert_eq!(instance.url.unwrap(), "#meta/foo.cm");
    assert!(!instance.is_cmx);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn show() {
    let explorer = connect_to_protocol::<fsys::RealmExplorerMarker>().unwrap();
    let query = connect_to_protocol::<fsys::RealmQueryMarker>().unwrap();

    let mut instances =
        show::find_instances("test.cm".to_string(), &explorer, &query).await.unwrap();

    assert_eq!(instances.len(), 1);
    let instance = instances.remove(0);

    assert!(instance.url.ends_with("#meta/test.cm"));
    assert!(instance.moniker.is_root());
    assert!(!instance.is_cmx);
    assert!(instance.resolved.is_some());
    let resolved = instance.resolved.unwrap();

    assert!(resolved.config.is_none());

    // The expected incoming capabilities are:
    // fidl.examples.routing.echo.Echo
    // fuchsia.foo.Bar
    // fuchsia.logger.LogSink
    // fuchsia.sys2.RealmExplorer
    // fuchsia.sys2.RealmQuery
    // hub
    assert_eq!(resolved.incoming_capabilities.len(), 6);

    // The expected exposed capabilities are:
    // fuchsia.foo.bar
    // fuchsia.test.Suite
    // minfs
    assert_eq!(resolved.exposed_capabilities.len(), 3);

    // We do not verify the contents of the execution, because they are largely dependent on
    // the Rust Test Runner
    assert!(resolved.started.is_some());

    let mut instances =
        show::find_instances("foo.cm".to_string(), &explorer, &query).await.unwrap();
    assert_eq!(instances.len(), 1);
    let instance = instances.remove(0);
    assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/foo").unwrap());
    assert_eq!(instance.url, "#meta/foo.cm");
    assert!(!instance.is_cmx);

    let resolved = instance.resolved.unwrap();
    assert!(resolved.started.is_none());

    // check foo's config
    let mut config = resolved.config.unwrap();
    assert_eq!(config.len(), 2);
    let field1 = config.remove(0);
    let field2 = config.remove(0);
    assert_eq!(field1.key, "my_string");
    assert_eq!(field1.value, "String(\"hello, world!\")");
    assert_eq!(field2.key, "my_uint8");
    assert_eq!(field2.value, "Uint8(255)");
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

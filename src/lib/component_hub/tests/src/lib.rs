// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_hub::{capability, list, show},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
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
    assert_eq!(resolved.incoming_capabilities.len(), 5);

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
    let explorer = connect_to_protocol::<fsys::RealmExplorerMarker>().unwrap();
    let query = connect_to_protocol::<fsys::RealmQueryMarker>().unwrap();

    let capability::MatchingInstances { mut exposed, mut used } =
        capability::find_instances_that_expose_or_use_capability(
            "fuchsia.foo.Bar".to_string(),
            &explorer,
            &query,
        )
        .await
        .unwrap();

    assert_eq!(exposed.len(), 1);
    assert_eq!(used.len(), 1);
    let exposed_component = exposed.remove(0);
    let used_component = used.remove(0);
    assert!(exposed_component.is_root());
    assert!(used_component.is_root());

    let capability::MatchingInstances { mut exposed, used } =
        capability::find_instances_that_expose_or_use_capability(
            "data".to_string(),
            &explorer,
            &query,
        )
        .await
        .unwrap();

    assert_eq!(exposed.len(), 1);
    assert_eq!(used.len(), 0);
    let exposed_component = exposed.remove(0);
    assert!(exposed_component.is_root());
}

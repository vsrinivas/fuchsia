// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_inspect::{
        reader::{NodeHierarchy, Property},
        testing::{assert_inspect_tree, AnyProperty, InspectDataFetcher},
    },
    log::info,
    selectors,
    std::collections::HashMap,
};

const TEST_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/stub_inspect_component.cm";

// TODO(fxb/54357): Move these into the core inspect library.
trait NodeHierarchyExt {
    fn get_child(&self, name: &str) -> &NodeHierarchy;
    fn get_property_str(&self, name: &str) -> &str;
}

impl NodeHierarchyExt for NodeHierarchy {
    fn get_child(&self, name: &str) -> &NodeHierarchy {
        self.children
            .iter()
            .find(|x| x.name == name)
            .expect(&format!("Failed to find {} node in the inspect hierarchy", name))
    }

    fn get_property_str(&self, name: &str) -> &str {
        for property in &self.properties {
            if let Property::String(key, value) = property {
                if key == name {
                    return value;
                }
            }
        }
        panic!(format!("Failed to find string property {}", name));
    }
}

// Verifies that archivist attributes logs from this component.
async fn verify_component_attributed(url: &str, expected_info_count: u64) {
    let mut response = InspectDataFetcher::new()
        .add_selector(format!(
            "archivist:root/log_stats/by_component/{}:*",
            selectors::sanitize_string_for_selectors(&url)
        ))
        .get()
        .await
        .unwrap();
    let hierarchy = response.pop().unwrap();
    let stats_node = hierarchy.get_child("log_stats").get_child("by_component");
    let component_stats = stats_node
        .children
        .iter()
        .map(|node| node.clone())
        .collect::<Vec<NodeHierarchy>>()
        .pop()
        .unwrap();
    let counts = component_stats
        .properties
        .iter()
        .map(|x| match x {
            Property::Int(name, value) => (name.as_str(), *value as u64),
            _ => ("", 0),
        })
        .collect::<HashMap<_, _>>();
    assert_eq!(expected_info_count, counts["info_logs"]);
}

#[fasync::run_singlethreaded(test)]
async fn read_v2_components_inspect() {
    let _test_app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT.to_string())
        .await
        .expect("Failed to create dynamic component");

    let data = InspectDataFetcher::new()
        .add_selector("driver/coll\\:auto-*:root")
        .get()
        .await
        .expect("got inspect data");

    assert_inspect_tree!(data[0], root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
}

// This test verifies that Archivist knows about logging from this component.
#[fasync::run_singlethreaded(test)]
async fn log_attribution() {
    fuchsia_syslog::init().unwrap();
    info!("This is a syslog message");
    info!("This is another syslog message");
    println!("This is a debuglog message");

    verify_component_attributed(
        "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/driver.cm",
        2,
    )
    .await;
}

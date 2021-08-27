// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::file_handler,
    anyhow::Error,
    log::*,
    serde_json::{json, Value as JsonValue},
};

// Make sure extremely deep-tree data doesn't overflow a stack.
const MAX_TREE_DEPTH: u32 = 128;

fn store_data(inspect_node: &fuchsia_inspect::Node, name: &str, data: &JsonValue, depth: u32) {
    if depth > MAX_TREE_DEPTH {
        return;
    }
    match data {
        JsonValue::String(value) => inspect_node.record_string(name, value),
        JsonValue::Number(value) => {
            if value.is_i64() {
                inspect_node.record_int(name, value.as_i64().unwrap());
            } else if value.is_u64() {
                inspect_node.record_uint(name, value.as_u64().unwrap());
            } else if value.is_f64() {
                inspect_node.record_double(name, value.as_f64().unwrap());
            }
        }
        JsonValue::Bool(value) => inspect_node.record_bool(name, *value),
        JsonValue::Object(object) => inspect_node.record_child(name, |node| {
            for (name, value) in object.iter() {
                store_data(node, name, value, depth + 1);
            }
        }),
        JsonValue::Null => {}
        // TODO(71350): If the array is all numbers, publish them (and test).
        JsonValue::Array(_) => {}
    }
}

pub fn serve_persisted_data(persist_root: &fuchsia_inspect::Node) -> Result<(), Error> {
    let remembered_data = file_handler::remembered_data()?;
    for (service_name, service_data) in remembered_data.iter() {
        persist_root.record_child(service_name, |service_node| {
            for (tag_name, tag_data) in service_data.iter() {
                let json_data = serde_json::from_str(tag_data).unwrap_or_else(|err| {
                    error!("Error {:?} parsing stored data", err);
                    json!("<<Error parsing saved data>>")
                });
                store_data(service_node, tag_name, &json_data, 0);
            }
        });
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Error;
    use fuchsia_inspect::{assert_data_tree, Inspector};

    // This tests all the types, and also the stack-safety mechanism by including data that should
    // be clipped.
    #[test]
    fn test_json_export() -> Result<(), Error> {
        let inspector = Inspector::new();
        let inspect = inspector.root();
        assert_data_tree!(
            inspector,
            root: contains {
            }
        );

        let data_json = "{ negint: -5, int: 42, unsigned: 0, float: 45.6, \
                         bool: true, obj: { child: 'child', grandchild: { hello: 'world', \
                         great_grand: { should_be_clipped: true } } } }";
        let mut data_parsed = serde_json5::from_str::<serde_json::Value>(data_json)?;
        // serde_json5::from_str() won't parse a number this big, so I have to insert it afterward.
        *data_parsed.get_mut("unsigned").unwrap() = serde_json::json!(9223372036854775808u64);
        store_data(inspect, "types", &data_parsed, MAX_TREE_DEPTH - 3);

        assert_data_tree!(
            inspector,
            root: {
                types: {
                    negint: -5i64,
                    int: 42i64,
                    unsigned: 9223372036854775808u64,
                    float: 45.6f64,
                    bool: true,
                    obj: {
                        child: "child",
                        grandchild: {
                            hello: "world",
                            great_grand: {}
                        }
                    }
                }
            }
        );
        Ok(())
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::screen::Line,
    anyhow::Result,
    diagnostics_data::InspectData,
    diagnostics_hierarchy::{self, hierarchy, InspectHierarchyMatcher},
    difference::{
        self,
        Difference::{Add, Rem, Same},
    },
    fidl_fuchsia_diagnostics::Selector,
    selectors::{self, VerboseError},
    std::{
        collections::HashSet,
        convert::TryInto,
        path::{Path, PathBuf},
    },
};

pub fn filter_json_schema_by_selectors(
    mut schema: InspectData,
    selectors: &Vec<Selector>,
) -> Option<InspectData> {
    let hierarchy = match schema.payload {
        Some(payload) => payload,
        None => {
            hierarchy! {
                root: {
                    "filter error": format!("Node hierarchy was missing for {}", schema.moniker),
                }
            }
        }
    };

    let moniker: Vec<_> = schema.moniker.split("/").map(|s| s.to_owned()).collect();
    match selectors::match_component_moniker_against_selectors(&moniker, selectors.as_slice()) {
        Ok(matched_selectors) => {
            if matched_selectors.is_empty() {
                return None;
            }

            let inspect_matcher: InspectHierarchyMatcher = matched_selectors.try_into().unwrap();

            match diagnostics_hierarchy::filter_hierarchy(hierarchy, &inspect_matcher) {
                Ok(Some(filtered)) => {
                    schema.payload = Some(filtered);
                    Some(schema)
                }
                Ok(None) => {
                    // Ok(None) implies the tree was fully filtered. This means that
                    // it genuinely should not be included in the output.
                    None
                }
                Err(e) => {
                    schema.payload = Some(hierarchy! {
                        root: {
                            "filter error": format!(
                                "Filtering the hierarchy of {}, an error occurred: {:?}",
                                schema.moniker, e
                            ),
                        }
                    });
                    Some(schema)
                }
            }
        }
        Err(e) => {
            schema.payload = Some(hierarchy! {
                root: {
                    "filter error": format!(
                        "Evaulating selectors for {} met an unexpected error condition: {:?}",
                        schema.moniker, e
                    ),
                }
            });
            Some(schema)
        }
    }
}

/// Consumes a file containing Inspect selectors and applies them to an array of node hierarchies
/// which had previously been serialized to their json schema.
///
/// Returns a vector of Line printed diffs between the unfiltered and filtered hierarchies,
/// or an Error.
pub fn filter_data_to_lines(
    selector_file: &Path,
    data: &[InspectData],
    moniker: &Option<String>,
) -> Result<Vec<Line>> {
    let selectors: Vec<Selector> =
        selectors::parse_selector_file::<VerboseError>(&PathBuf::from(selector_file))?
            .into_iter()
            .collect();

    // Filter the data to contain only moniker files we are interested in.
    let mut diffable_source = if let Some(moniker) = moniker {
        data.iter().filter(|schema| schema.moniker == *moniker).cloned().collect()
    } else {
        data.to_vec()
    };

    let mut filtered_node_hierarchies: Vec<InspectData> = diffable_source
        .clone()
        .into_iter()
        .filter_map(|schema| filter_json_schema_by_selectors(schema, &selectors))
        .collect();

    let moniker_cmp = |a: &InspectData, b: &InspectData| {
        a.moniker.partial_cmp(&b.moniker).expect("Schema comparison")
    };

    diffable_source.sort_by(moniker_cmp);
    filtered_node_hierarchies.sort_by(moniker_cmp);

    let sort_payload = |schema: &mut InspectData| {
        if let Some(ref mut payload) = schema.payload {
            payload.sort();
        }
    };

    diffable_source.iter_mut().for_each(sort_payload);
    filtered_node_hierarchies.iter_mut().for_each(sort_payload);

    let orig_str = serde_json::to_string_pretty(&diffable_source).unwrap();
    let new_str = serde_json::to_string_pretty(&filtered_node_hierarchies).unwrap();
    let cs = difference::Changeset::new(&orig_str, &new_str, "\n");

    // "Added" lines only appear when a property that was once in the middle of a
    // nested object, and thus ended its line with a comma, becomes the final property
    // in a node and thus loses the comma. The difference library doesn't expose edit distance
    // per-line, so we must instead track these "added" lines, and check if any of the "removed"
    // lines are one of the "added" lines with a comma on the end.
    let added_line_tracker: HashSet<&str> =
        cs.diffs.iter().fold(HashSet::new(), |mut acc, change| {
            if let Add(val) = change {
                acc.insert(val);
            }
            acc
        });

    Ok(cs
        .diffs
        .iter()
        .map(|change| match change {
            Same(val) | Add(val) => val.split("\n").map(|l| Line::new(l)).collect::<Vec<Line>>(),
            Rem(val) => val
                .split("\n")
                .filter_map(|l| {
                    let last_char_truncated: &str = &l[..l.len() - 1];
                    if !added_line_tracker.contains(last_char_truncated) {
                        Some(Line::removed(l))
                    } else {
                        None
                    }
                })
                .collect::<Vec<Line>>(),
        })
        .flatten()
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_inspect_test_utils::{
        get_empty_value_json, get_v1_json_dump, get_v1_single_value_json,
    };
    use fuchsia;
    use std::io::Write;
    use tempfile;

    fn setup_and_run_selector_filtering(
        selector_string: &str,
        source_hierarchy: serde_json::Value,
        golden_json: serde_json::Value,
        requested_moniker: Option<String>,
    ) {
        let mut selector_path =
            tempfile::NamedTempFile::new().expect("Creating tmp selector file should succeed.");

        selector_path
            .write_all(selector_string.as_bytes())
            .expect("writing selectors to file should be fine...");

        let schemas: Vec<InspectData> =
            serde_json::from_value(source_hierarchy).expect("load schemas");
        let filtered_data_string =
            filter_data_to_lines(&selector_path.path(), &schemas, &requested_moniker)
                .expect("filtering hierarchy should have succeeded.")
                .into_iter()
                .filter(|line| !line.removed)
                .fold(String::new(), |mut acc, line| {
                    acc.push_str(&line.value);
                    acc
                });
        let filtered_json_value: serde_json::Value = serde_json::from_str(&filtered_data_string)
            .expect(&format!(
                "Resultant json dump should be parsable json: {}",
                filtered_data_string
            ));

        assert_eq!(filtered_json_value, golden_json);
    }

    #[fuchsia::test]
    fn trailing_comma_diff_test() {
        let trailing_comma_hierarchy = serde_json::json!(
            [
                {
                    "data_source": "Inspect",
                    "metadata": {
                        "filename": "fuchsia.inspect.Tree",
                        "component_url": "fuchsia-pkg://fuchsia.com/blooper#meta/blooper.cmx",
                        "timestamp": 0
                    },
                    "moniker": "blooper.cmx",
                    "payload": {
                        "root": {
                            "a": {
                                "b": 0,
                                "c": 1
                            }
                        }
                    },
                    "version": 1
                }
            ]
        );

        let selector = "blooper.cmx:root/a:b";
        let mut selector_path =
            tempfile::NamedTempFile::new().expect("Creating tmp selector file should succeed.");

        selector_path
            .write_all(selector.as_bytes())
            .expect("writing selectors to file should be fine...");
        let mut schemas: Vec<InspectData> =
            serde_json::from_value(trailing_comma_hierarchy).expect("ok");
        for schema in schemas.iter_mut() {
            if let Some(hierarchy) = &mut schema.payload {
                hierarchy.sort();
            }
        }
        let filtered_data_string =
            filter_data_to_lines(&selector_path.path(), &schemas, &Some("blooper.cmx".to_string()))
                .expect("filtering hierarchy should succeed.");

        let removed_lines = filtered_data_string.iter().fold(HashSet::new(), |mut acc, line| {
            if line.removed {
                eprintln!("line removed bloop:{}", line.value.clone());
                acc.insert(line.value.clone());
            }
            acc
        });

        assert!(removed_lines.len() == 1);
        assert!(removed_lines.contains(&r#"          "c": 1"#.to_string()));
    }

    #[fuchsia::test]
    fn v1_filter_data_to_lines_test() {
        let full_tree_selector = "*/realm2/session5/account_manager.cmx:root/accounts:active
realm1/realm*/sessio*/account_manager.cmx:root/accounts:total
realm1/realm2/session5/account_manager.cmx:root/auth_providers:types
realm1/realm2/session5/account_manager.cmx:root/listeners:active
realm1/realm2/session5/account_*:root/listeners:events
realm1/realm2/session5/account_manager.cmx:root/listeners:total_opened";

        setup_and_run_selector_filtering(
            full_tree_selector,
            get_v1_json_dump(),
            get_v1_json_dump(),
            None,
        );

        setup_and_run_selector_filtering(
            full_tree_selector,
            get_v1_json_dump(),
            get_v1_json_dump(),
            Some("realm1/realm2/session5/account_manager.cmx".to_string()),
        );

        let single_value_selector =
            "realm1/realm2/session5/account_manager.cmx:root/accounts:active";

        setup_and_run_selector_filtering(
            single_value_selector,
            get_v1_json_dump(),
            get_v1_single_value_json(),
            None,
        );

        setup_and_run_selector_filtering(
            single_value_selector,
            get_v1_json_dump(),
            get_v1_single_value_json(),
            Some("realm1/realm2/session5/account_manager.cmx".to_string()),
        );

        setup_and_run_selector_filtering(
            single_value_selector,
            get_v1_json_dump(),
            get_empty_value_json(),
            Some("bloop.cmx".to_string()),
        );
    }
}

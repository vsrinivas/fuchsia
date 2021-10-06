// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_core::ffx_plugin,
    ffx_pdk_lib::groups::{ArtifactStore, ArtifactStoreEntry, ArtifactStoreGroup},
    ffx_pdk_lib::lock::{Lock, LockArtifact, LockArtifactStore},
    ffx_pdk_lib::spec::{Spec, SpecArtifactStore, SpecArtifactStoreKind},
    ffx_pdk_update_args::UpdateCommand,
    fuchsia_hyper::new_https_client,
    hyper::{body, StatusCode, Uri},
    serde_json::{json, Map, Value},
    serde_json5,
    std::cmp::Ordering,
    std::fs::{read_to_string, File, OpenOptions},
    std::io::BufReader,
};

// Outputs artifacts to a lock file based on a general specification.
//
// Updates the artifacts by matching the available artifacts in an
// artifact store against the constraints in a specification
// (artifact_spec.json).

// URL path to artifact_groups.json for tuf artifact store
//
const TUF_ARTIFACT_GROUPS_PATH: &str = "/targets/artifact_groups.json";

#[ffx_plugin("ffx_pdk")]
pub async fn cmd_update(cmd: UpdateCommand) -> Result<()> {
    let spec: Spec = read_to_string(cmd.spec_file.clone())
        .map_err(|e| ffx_error!("Cannot open file {:?} \nerror: {:?}", cmd.spec_file, e))
        .and_then(|contents| {
            serde_json5::from_str(&contents)
                .map_err(|e| ffx_error!("Spec json parsing errored {}", e))
        })?;
    process_spec(&spec, &cmd).await?;
    println!("Spec file for product \"{}\" processed.", spec.product);
    Ok(())
}

/// Struct to hold a JSON Pointer as specified in [RFC
/// 6901](https://tools.ietf.org/html/rfc6901) and a $min/$max boolean.
///
/// This struct is used for filtering artifact store by $min/$max.
///
struct MinMaxPointer {
    pointer: String,
    is_min: bool,
}

impl std::fmt::Debug for MinMaxPointer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "({}, {})", self.pointer, if self.is_min { "$min" } else { "$max" })
    }
}

/// Returns a MinMaxPointer containing a JSON Pointer and "$min" or "$max" value.
///
/// No more than one $min or $max is allowed, so check and return errors.
///
fn get_min_max_pointer(json_object: &Map<String, Value>) -> Result<Option<MinMaxPointer>> {
    let mut r = collect_min_max_pointers(json_object, "".to_string());
    match r.len() {
        0 => Ok(None),
        1 => Ok(Some(r.remove(0))),
        _ => ffx_bail!("More than one $min/$max found while processing spec file! {:?}", r),
    }
}

/// Recursively collect JSON Pointers for keys containing the string
/// value "$min" or "$max" in the spec attributes.
///
/// JSON Pointers are used to look up values from a Value::Object for
/// filtering artifact store entries.
///
/// Return a vec of MinMaxPointer structs and the caller checks that no
/// more than 1 struct is returned.
///
fn collect_min_max_pointers(json_object: &Map<String, Value>, path: String) -> Vec<MinMaxPointer> {
    // Collect in a tuple so we can catch the error of too many sort keys.
    let mut result = Vec::<MinMaxPointer>::new();
    for (key, value) in json_object.iter() {
        match value {
            Value::String(s) => {
                if s == "$min" || s == "$max" {
                    result.push(MinMaxPointer {
                        pointer: format!("{}/{}", path, key),
                        is_min: s == "$min",
                    })
                }
            }
            Value::Object(o) => {
                result.append(&mut collect_min_max_pointers(o, format!("{}/{}", path, key)));
            }
            Value::Null | Value::Bool(_) | Value::Number(_) | Value::Array(_) => {}
        }
    }
    result
}

/// Compare two Value::Object types using a JSON Pointer to extract the
/// comparison field.
///
/// Since this function is used by sort_by it returns Ordering.  Panics
/// if either comparison field is missing or the field is not a number
/// or string.
///
fn value_object_partial_cmp(
    a_object: &Value,
    b_object: &Value,
    pointer: &String,
) -> Option<Ordering> {
    // values must be available, otherwise fatal error
    let a: &Value = a_object
        .pointer(pointer)
        .unwrap_or_else(|| panic!("Missing field '{}' during $min/$max", pointer));
    let b: &Value = b_object
        .pointer(pointer)
        .unwrap_or_else(|| panic!("Missing field '{}' during $min/$max", pointer));
    match (a, b) {
        (Value::Number(na), Value::Number(nb)) => {
            na.as_f64().unwrap().partial_cmp(&nb.as_f64().unwrap())
        }
        (Value::String(sa), Value::String(sb)) => sa.partial_cmp(sb),
        (_, _) => panic!("$min/$max field ({}) is not Number or String: {} {}", pointer, a, b),
    }
}

/// Find the $min, $max and return the index.
///
fn find_min_max(
    artifact_groups: &Vec<ArtifactStoreGroup>,
    matches: &Vec<usize>,
    attributes: &Map<String, Value>,
) -> Result<usize> {
    // The next statement returns Err() when more than 1 $min/$max is present
    let min_max_pointer = get_min_max_pointer(attributes)?;
    match min_max_pointer {
        None => {
            if artifact_groups.len() > 1 {
                ffx_bail!("Multiple artifact groups (probably missing $min/$max)");
            }
            Ok(0)
        }
        Some(p) => Ok(*matches
            .iter()
            .max_by(|&a, &b| {
                let a_attributes = &artifact_groups[*a].attributes;
                let b_attributes = &artifact_groups[*b].attributes;
                value_object_partial_cmp(a_attributes, b_attributes, &p.pointer)
                    .map(|ordering| if p.is_min { ordering.reverse() } else { ordering })
                    .unwrap()
            })
            .unwrap()),
    }
}

/// Returns the artifact for an artifact store entry by name.
///
fn get_artifact(
    artifact_store_group: &ArtifactStoreGroup,
    name: &str,
) -> Option<ArtifactStoreEntry> {
    artifact_store_group.artifacts.iter().find(|&a| a.name == name).and_then(|a| Some(a.clone()))
}

/// Return artifact_groups.json for different kinds of artifact stores.
///
async fn read_artifact_groups(
    store: &SpecArtifactStore,
    cmd: &UpdateCommand,
) -> Result<ArtifactStore> {
    match store.r#type {
        SpecArtifactStoreKind::TUF => {
            if store.repo.is_none() {
                ffx_bail!("Missing repo field in artifact store")
            }
            let repo = store.repo.as_ref().unwrap();
            let uri = format!("https://{}/{}", repo, TUF_ARTIFACT_GROUPS_PATH).parse::<Uri>()?;
            let client = new_https_client();
            let response = client.get(uri.clone()).await?;
            if response.status() != StatusCode::OK {
                ffx_bail!("http get error {} {}. \n", &uri, response.status(),);
            }
            let bytes = body::to_bytes(response.into_body()).await?;
            let body = String::from_utf8(bytes.to_vec()).expect("response was not valid utf-8");
            Ok(serde_json::from_str(&body)?)
        }
        SpecArtifactStoreKind::Local => {
            if store.path.is_none() {
                ffx_bail!("Missing path field in store kind");
            }
            let path_suffix = store.path.as_ref().unwrap();
            if cmd.artifact_root.is_none() {
                ffx_bail!("Missing --artifact-root parameter");
            }
            let path = format!("{}/{}", cmd.artifact_root.as_ref().unwrap(), path_suffix);
            let reader = BufReader::new(File::open(path)?);
            Ok(serde_json::from_reader(reader)?)
        }
    }
}

/// Recursively match the artifact group attributes against the specification pattern.
///
/// True if a match.
///
fn match_object(group_attributes: &Value, spec_pattern: &Map<String, Value>) -> bool {
    if !group_attributes.is_object() {
        panic!("match_object: not an object.");
    }
    for (key, spec_value) in spec_pattern.iter() {
        if let Some(group_value) = group_attributes.get(key) {
            // Do not compare $min/$max spec values
            if *spec_value != json!("$min") && *spec_value != json!("$max") {
                if group_value.is_object() && spec_value.is_object() {
                    // Compare Object types recursively
                    if !match_object(group_value, spec_value.as_object().unwrap()) {
                        return false;
                    }
                } else if *group_value != *spec_value {
                    // Compare Bool, Number, String, Array
                    return false;
                };
            }
        } else {
            // No value for the key in the spec, probably a user error
            println!("Missing value during match for key \"{}\"", key);
            return false;
        }
    }
    true
}

/// Match artifacts groups from the artifact store file and spec attribute pattern.
///
/// Returns the index of the matching group.
///
fn match_artifacts(
    artifact_groups: &Vec<ArtifactStoreGroup>,
    spec_attribute_pattern: &Map<String, Value>,
) -> Result<usize> {
    let mut matches = Vec::<usize>::new();
    for (index, artifact_group) in artifact_groups.iter().enumerate() {
        if match_object(&artifact_group.attributes, spec_attribute_pattern) {
            matches.push(index);
        }
    }
    let index = find_min_max(&artifact_groups, &matches, &spec_attribute_pattern)?;
    Ok(index)
}

/// Merge two Option<Map> and return a new map. Entries are cloned.
///
/// Note: a duplicate key in b overwrites the value from a.
///
fn merge(a: &Option<Map<String, Value>>, b: &Option<Map<String, Value>>) -> Map<String, Value> {
    let mut result = Map::new();
    if let Some(map) = a {
        result.extend(map.into_iter().map(|(k, v)| (k.clone(), v.clone())));
    }
    if let Some(map) = b {
        result.extend(map.into_iter().map(|(k, v)| (k.clone(), v.clone())));
    }
    result
}

/// Main processing of a spec file
///
async fn process_spec(spec: &Spec, cmd: &UpdateCommand) -> Result<()> {
    let mut lock_artifacts = Vec::<LockArtifact>::new();
    for spec_artifact_group in spec.artifact_groups.iter() {
        // SpecArtifactGroup has a store and list of artifacts
        let spec_artifact_store = &spec_artifact_group.artifact_store;
        let artifact_store_groups = read_artifact_groups(&spec_artifact_store, cmd).await?;

        // find each artifact in the spec in the store
        for spec_artifact in spec_artifact_group.artifacts.iter() {
            let name = &spec_artifact.name;

            // Merge attributes from group and spec
            let attributes = merge(&spec.attributes, &spec_artifact_group.attributes);

            // Select the single group that matches
            let groups = &artifact_store_groups.artifact_groups;
            let matching_index: usize = match_artifacts(groups, &attributes)?;
            let matching_group = &groups[matching_index];

            let artifact_store_group_entry =
                get_artifact(matching_group, name).expect("missing artifiact");

            let artifact_output = LockArtifact {
                name: name.to_owned(),
                r#type: artifact_store_group_entry.r#type,
                artifact_store: LockArtifactStore {
                    name: spec_artifact_store.name.to_string(),
                    artifact_group_name: matching_group.name.to_string(),
                    r#type: spec_artifact_store.r#type.clone(),
                    repo: spec_artifact_store.repo.clone(),
                    content_address_storage: matching_group.content_address_storage.clone(),
                },
                attributes: matching_group.attributes.as_object().unwrap().clone(),
                // todo: rename to hash
                merkle: artifact_store_group_entry.hash,
                blobs: vec![],
            };
            lock_artifacts.push(artifact_output);
        }
    }
    let lock = Lock { artifacts: lock_artifacts };
    let file = OpenOptions::new().create(true).write(true).truncate(true).open(cmd.out.clone())?;

    // write file
    serde_json::to_writer_pretty(&file, &lock)?;

    Ok(())
}

// tests

#[cfg(test)]
mod test {
    use {super::*, serde_json::json, serde_json5};

    /// Test artifact hash
    #[test]
    fn test_get_hash() {
        // Test data in json5 format for cleaner look
        let data = r#"
          {
            name: "1361ee2a-e384-4eda-9f25-694affdeb30e",
            content_address_storage: "fuchsia-blobs.googleusercontent.com",
            type: "tuf",
            attributes: {version: "63"},
            artifacts: [
             { name: "one", merkle: "hash_1", sha256: "2", type: "package" },
             { name: "two", merkle: "hash_2", sha256: "3", type: "package" },
            ],
          }"#;

        // Parse the test data
        let v: ArtifactStoreGroup = serde_json5::from_str(data).unwrap();
        assert_eq!(get_artifact(&v, "one").unwrap().hash, "hash_1");
    }

    // For testing comparisons
    impl PartialEq for MinMaxPointer {
        fn eq(&self, other: &MinMaxPointer) -> bool {
            self.is_min == other.is_min && self.pointer == other.pointer
        }
    }

    #[test]
    fn test_get_min_max_pointer() {
        let object = json!({
            "name": "John",
            "age": {
                "human": "$max",
                "dog": 49,
            }
        });
        let ptr = get_min_max_pointer(&object.as_object().unwrap());
        // A Result containing an Option containing a tuple
        assert_eq!(
            ptr.unwrap().unwrap(),
            MinMaxPointer { pointer: "/age/human".to_string(), is_min: false }
        )
    }

    // Tests the filtering of artifact store groups by $min/$max
    //
    #[test]
    fn test_find_min_max() {
        let store: ArtifactStore = serde_json::from_str(
            r#"
          {
            "schema_version": "v1",
            "artifact_groups": [
              {
               "artifacts": [ ],
               "attributes": {
                  "creation_time": "2021-09-06T11:37:36.054280"
               },
              "name": "group_a"
             }, {
              "artifacts": [ ],
              "attributes": {
                 "creation_time": "2021-09-06T11:37:36.054281"
              },
              "name": "group_b"
            }
          ]
        }"#,
        )
        .unwrap();

        assert_eq!(store.artifact_groups.len(), 2);

        // The spec attributes for the $min case
        let json_min = json!({
            "creation_time": "$min"
        });
        // Convert to Map<String,Value> instead of Value.
        let spec_attributes_min = json_min.as_object().unwrap();
        let matches: Vec<usize> = (0..store.artifact_groups.len()).collect();

        assert_eq!(
            find_min_max(&store.artifact_groups, &matches, &spec_attributes_min).unwrap(),
            0
        );

        // max
        let json_max = json!({
            "creation_time": "$max"
        });
        let spec_attributes_max = json_max.as_object().unwrap();

        assert_eq!(
            find_min_max(&store.artifact_groups, &matches, &spec_attributes_max).unwrap(),
            1
        );
    }

    // Test match_object cases
    // - ignores $min/$max fields
    // - fails on top level object
    // - fails on recursive object
    #[test]
    fn test_match_object() {
        let spec_json = json!({"a": "$max", "b": 1, "c": {"d": true}});
        let spec = spec_json.as_object().unwrap();
        let group_1 = json!({"a": 1, "b": 1, "c": {"d": true}});
        assert!(match_object(&group_1, &spec));
        let group_2 = json!({"a": 1, "b": 2, "c": {"d": true}});
        assert!(!match_object(&group_2, &spec));
        let group_3 = json!({"a": 1, "b": 1, "c": {"d": false}});
        assert!(!match_object(&group_3, &spec));
        let group_4 = json!({"a": 1, "c": {"d": false}});
        assert!(!match_object(&group_4, &spec));
    }
}

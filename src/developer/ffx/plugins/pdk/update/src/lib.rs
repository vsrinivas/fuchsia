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
    fuchsia_pkg::MetaContents,
    futures_lite::io::AsyncWriteExt,
    hyper::body::HttpBody,
    hyper::{body, StatusCode, Uri},
    serde_json::{json, Map, Value},
    serde_json5,
    std::cmp::Ordering,
    std::fs::{read_to_string, File, OpenOptions},
    std::io::BufReader,
    std::path::PathBuf,
};

// Outputs artifacts to a lock file based on a general specification.
//
// Updates the artifacts by matching the available artifacts in an
// artifact store against the constraints in a specification
// (artifact_spec.json).

// URL path to artifact_groups.json for tuf artifact store
//
const TUF_ARTIFACT_GROUPS_PATH: &str = "targets/artifact_groups.json";

#[ffx_plugin("ffx_pdk")]
pub async fn cmd_update(cmd: UpdateCommand) -> Result<()> {
    let spec: Spec = read_to_string(cmd.spec_file.clone())
        .map_err(|e| ffx_error!(r#"Cannot open spec file "{}": {}"#, cmd.spec_file.display(), e))
        .and_then(|contents| {
            serde_json5::from_str(&contents).map_err(|e| {
                ffx_error!(r#"JSON5 error from spec file "{}": {}"#, cmd.spec_file.display(), e)
            })
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
            let uri = format!("{}/{}", repo, TUF_ARTIFACT_GROUPS_PATH)
                .parse::<Uri>()
                .map_err(|e| ffx_error!(r#"Parse Uri failed for "{}": {}"#, repo, e))?;
            let client = new_https_client();
            let response = client
                .get(uri.clone())
                .await
                .map_err(|e| ffx_error!(r#"Failed on http get for "{}": {}"#, uri, e))?;
            if response.status() != StatusCode::OK {
                ffx_bail!("http get error {} {}. \n", &uri, response.status(),);
            }
            let bytes = body::to_bytes(response.into_body()).await?;
            let body = String::from_utf8(bytes.to_vec()).expect("response was not valid utf-8");
            Ok(serde_json::from_str(&body)
                .map_err(|e| ffx_error!(r#"Cannot parse json from "{}": {}"#, &uri, e))?)
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
            let reader = BufReader::new(
                File::open(path.clone())
                    .map_err(|e| ffx_error!(r#"Cannot open "{}": {}"#, &path, e))?,
            );
            Ok(serde_json::from_reader(reader)
                .map_err(|e| ffx_error!(r#"Cannot parse json from "{}": {}"#, &path, e))?)
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

async fn get_blobs(
    content_address_storage: Option<String>,
    hash: String,
    artifact_root: Option<String>,
) -> Result<Vec<String>> {
    let tempdir = tempfile::tempdir().unwrap();
    let mut result = vec![hash.clone()];
    let meta_far_path = if content_address_storage.is_none() {
        PathBuf::from(artifact_root.unwrap()).join(hash.to_string())
    } else {
        let hostname = content_address_storage.unwrap();
        let uri = format!("{}/{}", hostname, hash)
            .parse::<Uri>()
            .map_err(|e| ffx_error!(r#"Parse Uri failed for "{}": {}"#, hostname, e))?;
        let client = new_https_client();
        let mut res = client
            .get(uri.clone())
            .await
            .map_err(|e| ffx_error!(r#"Failed on http get for "{}": {}"#, uri, e))?;
        let status = res.status();

        if status != StatusCode::OK {
            ffx_bail!("Cannot download meta.far. Status is {}. Uri is: {}.", status, &uri);
        }
        let meta_far_path = tempdir.path().join("meta.far");
        let mut output = async_fs::File::create(&meta_far_path).await?;
        while let Some(next) = res.data().await {
            let chunk = next?;
            output.write_all(&chunk).await?;
        }
        output.sync_all().await?;
        meta_far_path
    };

    let mut archive = File::open(&meta_far_path)
        .map_err(|e| ffx_error!(r#"Cannot open meta_far "{}": {}"#, meta_far_path.display(), e))?;
    let mut meta_far = fuchsia_archive::Reader::new(&mut archive).map_err(|e| {
        ffx_error!(r#"Cannot read fuchsia_archive "{}": {}"#, meta_far_path.display(), e)
    })?;
    let meta_contents = meta_far.read_file("meta/contents").map_err(|e| {
        ffx_error!(r#"Cannot read "meta/contens" from "{}": {}"#, meta_far_path.display(), e)
    })?;
    let meta_contents = MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    result.extend(meta_contents.into_iter().map(|(_, hash)| hash.to_string()));
    return Ok(result);
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
                merkle: artifact_store_group_entry.hash.clone(),
                blobs: get_blobs(
                    matching_group.content_address_storage.clone(),
                    artifact_store_group_entry.hash,
                    cmd.artifact_root.clone(),
                )
                .await?,
            };
            lock_artifacts.push(artifact_output);
        }
    }
    let lock = Lock { artifacts: lock_artifacts };
    let file = OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(cmd.out.clone())
        .map_err(|e| ffx_error!(r#"Cannot create lock file "{}": {}"#, cmd.out.display(), e))?;

    // write file
    serde_json::to_writer_pretty(&file, &lock)?;

    Ok(())
}

// tests

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use fuchsia_pkg::MetaPackage;
    use fuchsia_pkg::{build_with_file_system, CreationManifest, FileSystem};
    use maplit::{btreemap, hashmap};
    use pkg::repository::{RepositoryManager, RepositoryServer};
    use pkg::test_utils::make_writable_empty_repository;
    use serde_json::json;
    use serde_json5;
    use std::collections::HashMap;
    use std::convert::TryInto;
    use std::fs;
    use std::io;
    use std::io::Write;
    use std::net::Ipv4Addr;
    use std::path::PathBuf;
    use std::sync::Arc;

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

    #[test]
    fn test_value_object_partial_cmp() {
        let a = json!({"w": {"x": 1}});
        let b = json!({"w": {"x": 2}});
        let ordering = value_object_partial_cmp(&a, &b, &"/w/x".to_string());
        assert_eq!(ordering, Some(Ordering::Less));
    }

    struct FakeFileSystem {
        content_map: HashMap<String, Vec<u8>>,
    }

    impl<'a> FileSystem<'a> for FakeFileSystem {
        type File = &'a [u8];
        fn open(&'a self, path: &str) -> Result<Self::File, io::Error> {
            Ok(self.content_map.get(path).unwrap().as_slice())
        }
        fn len(&self, path: &str) -> Result<u64, io::Error> {
            Ok(self.content_map.get(path).unwrap().len() as u64)
        }
        fn read(&self, path: &str) -> Result<Vec<u8>, io::Error> {
            Ok(self.content_map.get(path).unwrap().clone())
        }
    }

    fn create_meta_far(path: PathBuf) {
        let creation_manifest = CreationManifest::from_external_and_far_contents(
            btreemap! {
                "lib/mylib.so".to_string() => "host/mylib.so".to_string()
            },
            btreemap! {
                "meta/my_component.cmx".to_string() => "host/my_component.cmx".to_string(),
                "meta/package".to_string() => "host/meta/package".to_string()
            },
        )
        .unwrap();
        let component_manifest_contents = "my_component.cmx contents";
        let mut v = vec![];
        let meta_package = MetaPackage::from_name("my-package-name".parse().unwrap());
        meta_package.serialize(&mut v).unwrap();
        let file_system = FakeFileSystem {
            content_map: hashmap! {
                "host/mylib.so".to_string() => Vec::new(),
                "host/my_component.cmx".to_string() => component_manifest_contents.as_bytes().to_vec(),
                "host/meta/package".to_string() => v
            },
        };

        build_with_file_system(&creation_manifest, &path, "my-package-name", &file_system).unwrap();
    }

    fn write_file(path: PathBuf, body: &[u8]) {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write(body).unwrap();
        tmp.persist(path).unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_end_to_end_local() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out_filename = root.join("artifact_lock.json");

        // recreate the test_data directory
        for (filename, data) in [
            ("artifact_spec.json", include_str!("../test_data/artifact_spec.json")),
            ("artifact_groups.json", include_str!("../test_data/artifact_groups.json")),
            ("artifact_groups2.json", include_str!("../test_data/artifact_groups2.json")),
        ] {
            fs::write(root.join(filename), data).expect("Unable to write file");
        }

        let meta_far_path =
            root.join("0000000000000000000000000000000000000000000000000000000000000000");
        create_meta_far(meta_far_path);
        let blob_path =
            root.join("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b");
        write_file(blob_path, "".as_bytes());

        let cmd = UpdateCommand {
            spec_file: PathBuf::from(root.join("artifact_spec.json")),
            out: out_filename.clone(),
            artifact_root: Some(root.display().to_string()),
        };

        let r = cmd_update(cmd).await;
        assert!(r.is_ok());
        let new_artifact_lock: Lock = File::open(&out_filename)
            .map(BufReader::new)
            .map(serde_json::from_reader)
            .unwrap()
            .unwrap();
        let golden_artifact_lock: Lock =
            serde_json::from_str(include_str!("../test_data/golden_artifact_lock.json")).unwrap();
        assert_eq!(new_artifact_lock, golden_artifact_lock);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_end_to_end_tuf() {
        let manager = RepositoryManager::new();
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path().join("artifact_store");
        let repo =
            make_writable_empty_repository("artifact_store", root.clone().try_into().unwrap())
                .await
                .unwrap();
        let out_filename = tempdir.path().join("artifact_lock.json");

        let meta_far_path = root
            .join("repository")
            .join("0000000000000000000000000000000000000000000000000000000000000000");
        create_meta_far(meta_far_path);
        let blob_path = root
            .join("repository")
            .join("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b");
        write_file(blob_path, "".as_bytes());

        manager.add(Arc::new(repo));

        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, _, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        let tuf_repo_url = server.local_url() + "/artifact_store";

        // write artifact_groups.json to server.
        let tuf_dir = root.join("repository").join("targets/");
        fs::create_dir(&tuf_dir).unwrap();
        let artifact_group_path = tuf_dir.join("artifact_groups.json");
        fs::write(
            artifact_group_path,
            include_str!("../test_data/tuf_artifact_groups.json")
                .replace("tuf_repo_url", &tuf_repo_url),
        )
        .unwrap();

        // write spec file.
        let spec_file_path = tempdir.path().join("artifact_spec.json");
        fs::write(
            &spec_file_path,
            include_str!("../test_data/tuf_artifact_spec.json")
                .replace("tuf_repo_url", &tuf_repo_url),
        )
        .unwrap();

        let cmd = UpdateCommand {
            spec_file: spec_file_path,
            out: out_filename.clone(),
            artifact_root: None,
        };

        cmd_update(cmd).await.unwrap();
        let new_artifact_lock: Lock = File::open(&out_filename)
            .map(BufReader::new)
            .map(serde_json::from_reader)
            .unwrap()
            .unwrap();
        let golden_artifact_lock: Lock = serde_json::from_str(
            include_str!("../test_data/golden_tuf_artifact_lock.json")
                .replace("tuf_repo_url", &tuf_repo_url)
                .as_str(),
        )
        .unwrap();
        assert_eq!(new_artifact_lock, golden_artifact_lock);

        // Signal the server to shutdown.
        server.stop();

        // Wait for the server to actually shut down.
        task.await;
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::hash::Hash;
use std::iter::{FromIterator, Iterator};

use structopt::StructOpt;

use sdk_metadata::{DartLibrary, ElementType, JsonObject, Manifest, Part};

mod app;
mod file_provider;
mod flags;
mod merge_dart_library;
mod tarball;

use crate::app::{Error, Result};
use crate::file_provider::FileProvider;
use crate::merge_dart_library::merge_dart_library;
use crate::tarball::{OutputTarball, SourceTarball};

const MANIFEST_PATH: &str = "meta/manifest.json";

/// Merges two given lists, removing duplicates and sorting the resulting list.
fn merge_lists<T>(one: &[T], two: &[T]) -> Vec<T>
where
    T: Ord + Clone + Eq + Hash,
{
    let mut joined: Vec<T> = one.to_vec().clone();
    joined.extend(two.iter().cloned());
    joined.sort_unstable();
    joined.dedup();
    joined
}

fn merge_manifests(base: &Manifest, complement: &Manifest) -> Result<Manifest> {
    let mut result = Manifest::default();

    // Host architecture.
    let has_host_content = |manifest: &Manifest| -> bool {
        manifest
            .parts
            .iter()
            .any(|part: &Part| part.kind == ElementType::HostTool)
    };
    let mut host_archs = HashSet::new();
    if has_host_content(&base) {
        host_archs.insert(base.arch.host.clone());
    }
    if has_host_content(&complement) {
        host_archs.insert(complement.arch.host.clone());
    }
    if host_archs.is_empty() {
        // The archives do not have any host content. The architecture is not meaningful in that
        // case but is still needed: just pick one.
        result.arch.host = base.arch.host.clone();
    } else if host_archs.len() == 1 {
        result.arch.host = host_archs
            .iter()
            .next()
            .expect("Should have 1 host arch")
            .clone();
    } else {
        let error = format!("Host architecture mismatch: {:?}", host_archs.iter());
        return Err(Error::CannotMerge { error })?;
    }

    // Target architecture.
    result.arch.target = merge_lists(&base.arch.target, &complement.arch.target);

    // Id.
    if base.id == complement.id {
        result.id = base.id.clone();
    } else {
        if base.id.is_empty() {
            result.id = complement.id.clone()
        } else if complement.id.is_empty() {
            result.id = base.id.clone();
        } else {
            let error = format!("Id mismatch: {} vs. {}", &base.id, &complement.id);
            return Err(Error::CannotMerge { error })?;
        }
    }

    // Parts.
    result.parts = merge_lists(&base.parts, &complement.parts);

    // Schema version.
    if base.schema_version != complement.schema_version {
        let error = format!(
            "Schema version mismatch: {} vs. {}",
            &base.schema_version, &complement.schema_version
        );
        return Err(Error::CannotMerge { error })?;
    }
    result.schema_version = base.schema_version.clone();

    result.validate()?;
    Ok(result)
}

/// This is a temporary method that allows unimplemented element types to be skipped over.
// TODO(DX-1056): delete when all types are supported.
fn is_kind_supported(kind: &ElementType) -> bool {
    match kind {
        ElementType::DartLibrary => true,
        _ => false,
    }
}

fn merge_common_part(
    part: &Part, base: &SourceTarball, complement: &SourceTarball, output: &mut OutputTarball,
) -> Result<()> {
    if !is_kind_supported(&part.kind) {
        println!("Skipping {}", part);
        return Ok(());
    }
    match part.kind {
        ElementType::DartLibrary => merge_dart_library(&part.meta, base, complement, output),
        _ => unreachable!("Type should have been skipped over: {:?}", part.kind),
    }
}

fn copy_part_as_is(part: &Part, source: &SourceTarball, output: &mut OutputTarball) -> Result<()> {
    if !is_kind_supported(&part.kind) {
        println!("Skipping {}", part);
        return Ok(());
    }
    println!("Copying {}", part);
    let provider: Box<dyn FileProvider> = Box::new(match part.kind {
        ElementType::DartLibrary => source.get_metadata::<DartLibrary>(&part.meta)?,
        _ => unreachable!("Type should have been skipped over: {:?}", part.kind),
    });
    let mut paths = provider.get_all_files();
    paths.push(part.meta.clone());
    for path in &paths {
        source.get_file(path, |file| output.write_file(path, file))?;
    }

    Ok(())
}

fn main() -> Result<()> {
    let flags = flags::Flags::from_args();

    let base = SourceTarball::new(&flags.base)?;
    let complement = SourceTarball::new(&flags.complement)?;
    let mut output = OutputTarball::new(&flags.output)?;

    let base_manifest: Manifest = base.get_metadata(MANIFEST_PATH)?;
    let complement_manifest: Manifest = complement.get_metadata(MANIFEST_PATH)?;

    let base_parts: HashSet<Part> = HashSet::from_iter(base_manifest.parts.iter().cloned());
    let complement_parts: HashSet<Part> =
        HashSet::from_iter(complement_manifest.parts.iter().cloned());

    println!("Common parts");
    for part in base_parts.intersection(&complement_parts) {
        merge_common_part(&part, &base, &complement, &mut output)?;
    }

    println!("Unique base parts");
    for part in base_parts.difference(&complement_parts) {
        copy_part_as_is(&part, &base, &mut output)?;
    }

    println!("Unique complement parts");
    for part in complement_parts.difference(&base_parts) {
        copy_part_as_is(&part, &complement, &mut output)?;
    }

    let merged_manifest = merge_manifests(&base_manifest, &complement_manifest)?;

    output.write_json(&MANIFEST_PATH.to_string(), &merged_manifest)?;
    output.export()?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use serde_json::value::Value;
    use serde_json::{from_value, json};

    use sdk_metadata::Manifest;

    use super::*;

    type Verifier = FnOnce(&Manifest) -> bool;

    macro_rules! test_merge {
        (
            name = $name:ident,
            base = $base:expr,
            complement = $complement:expr,
            success = $success:expr,
        ) => {
            #[test]
            fn $name() {
                merge_test($base, $complement, $success, None);
            }
        };
        (
            name = $name:ident,
            base = $base:expr,
            complement = $complement:expr,
            success = $success:expr,
            verifier = $verifier:expr,
        ) => {
            #[test]
            fn $name() {
                merge_test($base, $complement, $success, Some(Box::new($verifier)));
            }
        };
    }

    fn merge_test(base: Value, complement: Value, success: bool, verifier: Option<Box<Verifier>>) {
        let base_manifest: Manifest = from_value(base).unwrap();
        base_manifest.validate().unwrap();
        let complement_manifest: Manifest = from_value(complement).unwrap();
        complement_manifest.validate().unwrap();
        let merged_manifest = merge_manifests(&base_manifest, &complement_manifest);
        assert_eq!(merged_manifest.is_ok(), success);
        if success {
            if let Some(verify) = verifier {
                assert!(verify(&merged_manifest.unwrap()));
            }
        }
    }

    test_merge!(
        name = test_clean_merge,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "foo/bar.json",
                "type": "dart_library",
            },
            {
                "meta": "alpha/beta.json",
                "type": "host_tool",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["arm64"] },
          "parts": [
            {
                "meta": "ping/pong.json",
                "type": "cc_prebuilt_library",
            },
            {
                "meta": "one/two.json",
                "type": "host_tool",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| {
            (manifest.arch.host == "x86_64-linux-gnu")
                & (manifest.arch.target.len() == 2)
                & (manifest.id == "bleh")
                & (manifest.schema_version == "1")
                & (manifest.parts.len() == 4)
        },
    );

    test_merge!(
        name = test_different_schema_versions,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "bleh",
          "schema_version": "2",
        }),
        success = false,
    );

    test_merge!(
        name = test_different_ids,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "whoops",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = false,
    );

    test_merge!(
        name = test_empty_id,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "whoops",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.id == "whoops" },
    );

    test_merge!(
        name = test_two_empty_ids,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.id == "" },
    );

    test_merge!(
        name = test_different_host_architectures,
        base = json!({
          "arch": { "host": "arm64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "foo/bar.json",
                "type": "host_tool",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "ping/pong.json",
                "type": "host_tool",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = false,
    );

    test_merge!(
        name = test_different_host_architectures_one_with_host_content,
        base = json!({
          "arch": { "host": "arm64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "ping/pong.json",
                "type": "dart_library",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [
              {
                  "meta": "foo/bar.json",
                  "type": "host_tool",
              },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.arch.host == "x86_64-linux-gnu" },
    );

    test_merge!(
        name = test_different_host_architectures_none_with_host_content,
        base = json!({
          "arch": { "host": "arm64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "foo/bar.json",
                "type": "cc_prebuilt_library",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "ping/pong.json",
                "type": "dart_library",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.arch.host == "arm64-linux-gnu" },
    );

    test_merge!(
        name = test_parts,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "foo/bar.json",
                "type": "cc_prebuilt_library",
            },
            {
                "meta": "ping/pong.json",
                "type": "dart_library",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [
            {
                "meta": "ping/pong.json",
                "type": "dart_library",
            },
            {
                "meta": "one/two.json",
                "type": "cc_source_library",
            },
          ],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.parts.len() == 3 },
    );

    test_merge!(
        name = test_same_target_architecture,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "whoops",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "whoops",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.arch.target.len() == 1 },
    );

    test_merge!(
        name = test_different_target_architectures,
        base = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["arm64"] },
          "parts": [],
          "id": "whoops",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "x86_64-linux-gnu", "target": ["x64"] },
          "parts": [],
          "id": "whoops",
          "schema_version": "1",
        }),
        success = true,
        verifier = |manifest: &Manifest| { manifest.arch.target.len() == 2 },
    );
}

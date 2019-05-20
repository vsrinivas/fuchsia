// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::iter::{FromIterator, Iterator};

use structopt::StructOpt;

use sdk_metadata::{JsonObject, Manifest, Part};

mod app;
mod flags;
mod tarball;

use crate::app::Result;
use crate::tarball::{OutputTarball, SourceTarball};

const MANIFEST_PATH: &str = "meta/manifest.json";

fn merge_manifests(_base: &Manifest, _complement: &Manifest) -> Result<Manifest> {
    let result: Manifest = Default::default();
    // TODO(DX-1056): perform some meaningful merging operation here.
    result.validate()?;
    Ok(result)
}

fn merge_common_part(
    part: &Part, _base: &SourceTarball, _complement: &SourceTarball, _output: &OutputTarball,
) -> Result<()> {
    // TODO(DX-1056): implement me.
    println!(" - {}", part);
    Ok(())
}

fn copy_part_as_is(part: &Part, _source: &SourceTarball, _output: &OutputTarball) -> Result<()> {
    // TODO(DX-1056): implement me.
    println!(" - {}", part);
    Ok(())
}

fn main() -> Result<()> {
    let flags = flags::Flags::from_args();

    let mut base = SourceTarball::new(flags.base)?;
    let mut complement = SourceTarball::new(flags.complement)?;
    let mut output = OutputTarball::new();

    let base_manifest: Manifest = base.get_metadata(MANIFEST_PATH)?;
    let complement_manifest: Manifest = complement.get_metadata(MANIFEST_PATH)?;

    let base_parts: HashSet<Part> = HashSet::from_iter(base_manifest.parts.iter().cloned());
    let complement_parts: HashSet<Part> =
        HashSet::from_iter(complement_manifest.parts.iter().cloned());

    println!("Common parts");
    for part in base_parts.intersection(&complement_parts) {
        merge_common_part(&part, &base, &complement, &output)?;
    }

    println!("Unique base parts");
    for part in base_parts.difference(&complement_parts) {
        copy_part_as_is(&part, &base, &output)?;
    }

    println!("Unique complement parts");
    for part in complement_parts.difference(&base_parts) {
        copy_part_as_is(&part, &complement, &output)?;
    }

    let merged_manifest = merge_manifests(&base_manifest, &complement_manifest)?;
    merged_manifest.validate()?;

    output.write(MANIFEST_PATH.to_string(), merged_manifest.to_string()?)?;
    output.export(flags.output)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use serde_json::value::Value;
    use serde_json::{from_value, json};

    use sdk_metadata::Manifest;

    use super::*;

    macro_rules! test_merge {
        (
            name = $name:ident,
            base = $base:expr,
            complement = $complement:expr,
            success = $success:expr,
        ) => {
            #[test]
            fn $name() {
                merge_test($base, $complement, $success);
            }
        };
    }

    fn merge_test(base: Value, complement: Value, success: bool) {
        let base_manifest: Manifest = from_value(base).unwrap();
        base_manifest.validate().unwrap();
        let complement_manifest: Manifest = from_value(complement).unwrap();
        complement_manifest.validate().unwrap();
        let merged_manifest = merge_manifests(&base_manifest, &complement_manifest);
        assert_eq!(merged_manifest.is_ok(), success);
    }

    test_merge!(
        name = test_just_an_experiment,
        base = json!({
          "arch": { "host": "foobarblah", "target": ["x64"] },
          "parts": [],
          "id": "bleh",
          "schema_version": "1",
        }),
        complement = json!({
          "arch": { "host": "foobarblah", "target": ["x64"] },
          "parts": [],
          "id": "bleh",
          "schema_version": "1",
        }),
        success = true,
    );
}

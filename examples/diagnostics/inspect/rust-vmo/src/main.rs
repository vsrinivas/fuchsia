// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    futures::stream::StreamExt,
    std::str::FromStr,
    structopt::{self, StructOpt},
};

static MAX_VMO_SIZE: usize = 100_000_000;

/// Create and serve a VMO that is at least `vmo_size` in size.
#[derive(Debug, StructOpt)]
struct ProgramArgs {
    /// The size of the Inspect VMO to create.
    vmo_size: InspectVmoSize,
}

/// The size of the Inspect VMO to be exposed, in bytes.
#[derive(Debug, PartialEq)]
struct InspectVmoSize(usize);

impl FromStr for InspectVmoSize {
    type Err = String;
    fn from_str(s: &str) -> Result<Self, String> {
        let parsed = s.parse::<usize>().or_else(|e| Err(format!("{:?}", e)))?;
        validate_vmo_size(parsed)?;
        Ok(InspectVmoSize(parsed))
    }
}

fn validate_vmo_size(size: usize) -> Result<(), String> {
    if size > MAX_VMO_SIZE {
        Err(format!("vmo_size {} is greater than maximum {}", size, MAX_VMO_SIZE))
    } else if size == 0 {
        Err(String::from("vmo_size cannot be 0"))
    } else {
        Ok(())
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args = ProgramArgs::from_args();

    let InspectVmoSize(vmo_size) = args.vmo_size;
    let inspector = inspect::Inspector::new_with_size(vmo_size);

    // Ensures that the Inspector is actually exposing information on the hub. Most components do
    // not need to do this since Inspect is best-effort (it will not crash your component if it
    // fails to initialize). We have this check because the whole point of this component is to
    // expose Inspect information on the hub.
    assert!(inspector.is_valid());

    // Retain properties so that they are not deleted from the VMO.
    let _properties = build_hierarchy(inspector.root(), vmo_size);

    let mut fs = ServiceFs::new();
    inspector.serve(&mut fs)?;
    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;

    Ok(())
}

// Builds a hierarchy under the given node that is guaranteed to fill up at least |vmo_size| bytes.
//
// Returns the properties so they can be retained, otherwise they would be deleted.
#[must_use]
fn build_hierarchy(node: &inspect::Node, vmo_size: usize) -> Vec<inspect::IntProperty> {
    let mut elements = vec![];
    let mut size = 0;
    while size < vmo_size {
        // Keep pushing elements until we hit the desired size.
        // Elements have a 16 byte node, 16 byte value, and 16 byte name.
        elements.push(node.create_int(format!("{}", size), size as i64));
        size += 16 * 3;
    }
    elements
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;

    #[test]
    fn build_hierarchy_sizes() {
        // Make 1MiB inspector to hold all hierarchies.
        let inspector = inspect::Inspector::new_with_size(1024 * 1024);

        let node_256 = inspector.root().create_child("node_256");
        let node_4096 = inspector.root().create_child("node_4096");
        let node_256k = inspector.root().create_child("node_256k");

        let mut properties = vec![];
        properties.append(&mut build_hierarchy(&node_256, 256));
        properties.append(&mut build_hierarchy(&node_4096, 4096));
        properties.append(&mut build_hierarchy(&node_256k, 256 * 1024));

        assert_eq!(
            256 / 16 / 3 + 4096 / 16 / 3 + 256 * 1024 / 16 / 3 + 3, /* remainders */
            properties.len()
        );

        assert_inspect_tree!(inspector, root: {
            node_256:
                contains {
                    "0": 0i64,
                    "240": 240i64,
                }
            ,
            node_4096: contains {
                "0": 0i64,
                "4080": 4080i64,
            },
            node_256k: contains {
                "0": 0i64,
                "262128": 262_128i64,
            }
        });
    }

    #[test]
    fn vmo_size_arg() {
        assert_eq!(
            InspectVmoSize(200_000),
            InspectVmoSize::from_str("200000").expect("200K is valid")
        );
        assert_eq!(InspectVmoSize(64), InspectVmoSize::from_str("64").expect("64 is valid"));
    }

    #[test]
    fn invalid_vmo_size_arg() {
        InspectVmoSize::from_str("200000000").expect_err("200M should fail");
        InspectVmoSize::from_str("300000000").expect_err("300M should fail");
        InspectVmoSize::from_str("400000000").expect_err("400M should fail");
        InspectVmoSize::from_str("one").expect_err("text should fail");
        InspectVmoSize::from_str("0").expect_err("zero should fail");
    }
}

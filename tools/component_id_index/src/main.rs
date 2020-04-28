// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::str;
use structopt::StructOpt;
use thiserror::Error;

#[derive(Debug, StructOpt)]
#[structopt(about = "Verify and merge component ID index files.")]
struct CommandLineOpts {
    #[structopt(
        short,
        long,
        help = "Path to a manifest text file containing a list of index files, one on each line. All index files are merged into a single index, written to the supplied --output_file"
    )]
    input_manifest: PathBuf,

    #[structopt(short, long, help = "Where to write the merged index file.")]
    output_file: PathBuf,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
struct AppmgrMoniker {
    url: String,
    realm_path: Vec<String>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
struct InstanceIdEntry {
    instance_id: String,
    appmgr_moniker: AppmgrMoniker,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct Index {
    instances: Vec<InstanceIdEntry>,
}

impl Index {
    fn from_file(path: &str) -> anyhow::Result<Index> {
        let contents = fs::read_to_string(path).context("unable to read file")?;
        json5::from_str::<Index>(&contents).context("unable to parse to json5")
    }
}

// MergeContext maintains a single merged index, along with some state for error checking, as indicies are merged together using MergeContext::merge().
//
// Usage:
// - Use MergeContext::new() to create a MergeContext.
// - Call MergeContext::merge() to merge an index. Can be called multiple times.
// - Call MergeContext::output() to access the merged index.
pub struct MergeContext {
    output_index: Index,
    // MergeConetext::merge() will accumulate the instance IDs which have been merged so far, along with the index source file which they came from.
    // This is used to validate that all instance IDs are unique and provide helpful error messages.
    // instance id -> path of file defining instance ID.
    accumulated_instance_ids: HashMap<String, String>,
}

#[derive(Error, Debug, PartialEq)]
enum MergeError {
    #[error("Instance ID '{}' must be unique but exists in following index files:\n {}\n {}", .instance_id, .source1, .source2)]
    DuplicateIds { instance_id: String, source1: String, source2: String },
}

impl MergeContext {
    fn new() -> MergeContext {
        MergeContext {
            output_index: Index { instances: vec![] },
            accumulated_instance_ids: HashMap::new(),
        }
    }

    // merge() merges `index` into the MergeContext.
    // This method can be called multiple times to merge multiple indicies.
    // The accumulated index can be accessed with output().
    fn merge(&mut self, source_index_path: &str, index: &Index) -> Result<(), MergeError> {
        for instance in &index.instances {
            if let Some(previous_source_path) = self
                .accumulated_instance_ids
                .insert(instance.instance_id.clone(), source_index_path.to_string())
            {
                return Err(MergeError::DuplicateIds {
                    instance_id: instance.instance_id.clone(),
                    source1: previous_source_path.clone(),
                    source2: source_index_path.to_string(),
                });
            }

            self.output_index.instances.push(instance.clone());
        }
        Ok(())
    }

    // Access the accumulated index from calls to merge().
    fn output(self) -> Index {
        self.output_index
    }
}

fn run(input_manifest_path: PathBuf, output_index_path: PathBuf) -> anyhow::Result<()> {
    let mut ctx = MergeContext::new();
    let input_manifest =
        fs::File::open(input_manifest_path).context("Could not open input manifest")?;
    let input_files = BufReader::new(input_manifest).lines();
    for input_file_path in input_files {
        let path = input_file_path.context("Could not read line from input manifest")?;
        let index = Index::from_file(&path)
            .with_context(|| anyhow!("Could not parse index file {}", &path))?;
        ctx.merge(&path, &index)
            .with_context(|| anyhow!("Could not merge index file {}", &path))?;
    }

    let serialized_output =
        json5::to_string(&ctx.output()).context("Could not json-encode merged index")?;
    fs::write(output_index_path, serialized_output.as_bytes())
        .context("Could not write merged index to file")
}

fn main() -> anyhow::Result<()> {
    let opts = CommandLineOpts::from_args();
    run(opts.input_manifest, opts.output_file)
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::*;
    use std::io::Write;
    use tempfile;

    fn gen_instance_id() -> String {
        // generate random 256bits into a byte array
        let mut rng = rand::thread_rng();
        let mut num: [u8; 256 / 8] = [0; 256 / 8];
        rng.fill_bytes(&mut num);
        // turn the byte array into a hex string
        num.iter().map(|byte| format!("{:x}", byte)).collect::<Vec<String>>().join("")
    }

    fn gen_index(num_instances: u32) -> Index {
        Index {
            instances: (0..num_instances)
                .map(|i| InstanceIdEntry {
                    instance_id: gen_instance_id(),
                    appmgr_moniker: AppmgrMoniker {
                        url: format!(
                            "fuchsia-pkg://example.com/fake_pkg#meta/fake_component_{}.cmx",
                            i
                        ),
                        realm_path: vec!["root".to_string(), "child".to_string(), i.to_string()],
                    },
                })
                .collect(),
        }
    }

    #[test]
    fn merge_empty_index() {
        let ctx = MergeContext::new();
        assert_eq!(ctx.output(), gen_index(0));
    }

    #[test]
    fn merge_single_index() {
        let mut ctx = MergeContext::new();
        let index = gen_index(0);
        ctx.merge("/random/file/path", &index).unwrap();
        assert_eq!(ctx.output(), index.clone());
    }

    #[test]
    fn merge_duplicate_ids() {
        let source1 = "/a/b/c";
        let source2 = "/d/e/f";

        let index1 = gen_index(1);
        let mut index2 = index1.clone();
        index2.instances[0].instance_id = index1.instances[0].instance_id.clone();

        let mut ctx = MergeContext::new();
        ctx.merge(source1, &index1).unwrap();

        let err = ctx.merge(source2, &index2).unwrap_err();

        assert_eq!(
            err,
            MergeError::DuplicateIds {
                instance_id: index1.instances[0].instance_id.clone(),
                source1: source1.to_string(),
                source2: source2.to_string()
            }
        );
    }

    #[test]
    fn index_from_json_file() {
        let mut tmp_input_manifest = tempfile::NamedTempFile::new().unwrap();
        let mut tmp_input_index1 = tempfile::NamedTempFile::new().unwrap();
        let mut tmp_input_index2 = tempfile::NamedTempFile::new().unwrap();
        let tmp_output_manifest = tempfile::NamedTempFile::new().unwrap();

        // the manifest lists two index files:
        write!(
            tmp_input_manifest,
            "{}\n{}",
            tmp_input_index1.path().display(),
            tmp_input_index2.path().display()
        )
        .unwrap();

        // write the first index file
        let index1 = gen_index(2);
        tmp_input_index1.write_all(json5::to_string(&index1).unwrap().as_bytes()).unwrap();

        // write the second index file
        let index2 = gen_index(2);
        tmp_input_index2.write_all(json5::to_string(&index2).unwrap().as_bytes()).unwrap();

        assert!(matches!(
            run(tmp_input_manifest.path().to_path_buf(), tmp_output_manifest.path().to_path_buf()),
            Ok(_)
        ));

        // assert that the output index file contains the merged index.
        let mut ctx = MergeContext::new();
        ctx.merge(&tmp_input_index1.path().to_str().unwrap(), &index1).unwrap();
        ctx.merge(&tmp_input_index2.path().to_str().unwrap(), &index2).unwrap();
        assert_eq!(
            ctx.output(),
            Index::from_file(&tmp_output_manifest.path().to_str().unwrap()).unwrap()
        );
    }
}

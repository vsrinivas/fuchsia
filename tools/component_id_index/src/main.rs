// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};

use std::collections::HashMap;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::str;
use structopt::StructOpt;
use thiserror::Error;

mod index;
use index::{is_valid_instance_id, GenerateInstanceIds, Index, InstanceIdEntry};

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

    #[structopt(
        short,
        long,
        help = "Where to write a dep file (.d) file of indices from --input_manifest."
    )]
    depfile: PathBuf,
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
    #[error("{}", .entries)]
    MissingInstanceIds { entries: GenerateInstanceIds },
    #[error("The following entry's instance_id is invalid (must be 64 lower-cased hex chars): {:?}", .entry)]
    InvalidInstanceId { entry: InstanceIdEntry },
    #[error("appmgr_restrict_isolated_persistent_storage has already been set to {} and cannot be set twice.", .previous_val)]
    MultipleStorageRestrictions { previous_val: bool },
}

impl MergeContext {
    fn new() -> MergeContext {
        MergeContext {
            output_index: Index {
                appmgr_restrict_isolated_persistent_storage: None,
                instances: vec![],
            },
            accumulated_instance_ids: HashMap::new(),
        }
    }

    // merge() merges `index` into the MergeContext.
    // This method can be called multiple times to merge multiple indicies.
    // The accumulated index can be accessed with output().
    fn merge(&mut self, source_index_path: &str, index: &Index) -> Result<(), MergeError> {
        let mut missing_instance_ids = vec![];
        for entry in &index.instances {
            match entry.instance_id.as_ref() {
                None => {
                    // Instead of failing right away, continue processing the other entries.
                    missing_instance_ids.push(entry.clone());
                    continue;
                }
                Some(instance_id) => {
                    if !is_valid_instance_id(&instance_id) {
                        return Err(MergeError::InvalidInstanceId { entry: entry.clone() });
                    }
                    if let Some(previous_source_path) = self
                        .accumulated_instance_ids
                        .insert(instance_id.clone(), source_index_path.to_string())
                    {
                        return Err(MergeError::DuplicateIds {
                            instance_id: instance_id.clone(),
                            source1: previous_source_path.clone(),
                            source2: source_index_path.to_string(),
                        });
                    }
                    self.output_index.instances.push(entry.clone());
                }
            }
        }
        if let Some(val) = index.appmgr_restrict_isolated_persistent_storage {
            if let Some(previous_val) =
                self.output_index.appmgr_restrict_isolated_persistent_storage
            {
                return Err(MergeError::MultipleStorageRestrictions { previous_val });
            } else {
                self.output_index.appmgr_restrict_isolated_persistent_storage = Some(val);
            }
        }
        if missing_instance_ids.len() > 0 {
            Err(MergeError::MissingInstanceIds {
                entries: GenerateInstanceIds::new(&mut rand::thread_rng(), missing_instance_ids),
            })
        } else {
            Ok(())
        }
    }

    // Access the accumulated index from calls to merge().
    fn output(self) -> Index {
        self.output_index
    }
}

fn run(opts: CommandLineOpts) -> anyhow::Result<()> {
    let mut ctx = MergeContext::new();
    let input_manifest =
        fs::File::open(opts.input_manifest).context("Could not open input manifest")?;
    let input_files = BufReader::new(input_manifest)
        .lines()
        .collect::<Result<Vec<String>, _>>()
        .context("Could not read input manifest")?;
    for input_file_path in &input_files {
        let index = Index::from_file(&input_file_path)
            .with_context(|| anyhow!("Could not parse index file {}", &input_file_path))?;
        ctx.merge(&input_file_path, &index)
            .with_context(|| anyhow!("Could not merge index file {}", &input_file_path))?;
    }

    // write out the merged index
    let serialized_output =
        serde_json5::to_string(&ctx.output()).context("Could not json-encode merged index")?;
    fs::write(&opts.output_file, serialized_output.as_bytes())
        .context("Could not write merged index to file")?;

    // write out the depfile
    fs::write(
        &opts.depfile,
        format!("{}: {}\n", opts.output_file.to_str().unwrap(), input_files.join(" ")),
    )
    .context("Could not write to depfile")
}

fn main() -> anyhow::Result<()> {
    let opts = CommandLineOpts::from_args();
    run(opts)
}

#[cfg(test)]
mod tests {
    use super::*;
    use index::*;
    use std::io::Write;
    use tempfile;

    fn gen_index(num_instances: u32) -> Index {
        Index {
            appmgr_restrict_isolated_persistent_storage: None,
            instances: (0..num_instances)
                .map(|i| InstanceIdEntry {
                    instance_id: Some(gen_instance_id(&mut rand::thread_rng())),
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
                instance_id: index1.instances[0].instance_id.as_ref().unwrap().clone(),
                source1: source1.to_string(),
                source2: source2.to_string()
            }
        );
    }

    #[test]
    fn multiple_appmgr_restrict_isolated_persistent_storage() {
        let source1 = "/a/b/c";
        let source2 = "/d/e/f";

        let mut index1 = gen_index(0);
        index1.appmgr_restrict_isolated_persistent_storage = Some(true);
        let mut index2 = index1.clone();
        index2.appmgr_restrict_isolated_persistent_storage = Some(false);

        let mut ctx = MergeContext::new();
        ctx.merge(source1, &index1).unwrap();
        let err = ctx.merge(source2, &index2).unwrap_err();

        assert_eq!(err, MergeError::MultipleStorageRestrictions { previous_val: true });
    }

    #[test]
    fn missing_instance_ids() {
        let mut index = gen_index(4);
        index.instances[1].instance_id = None;
        index.instances[3].instance_id = None;

        let mut ctx = MergeContext::new();
        // this should be an error, since `index` has entries with a missing instance ID.
        let merge_result: Result<(), MergeError> = ctx.merge("/a/b/c", &index);
        let mut fixed_entries = match merge_result.as_ref() {
            Err(MergeError::MissingInstanceIds { entries: GenerateInstanceIds(fixed_entries) }) => {
                fixed_entries.clone()
            }
            _ => panic!("Expected merge failure MissingInstanceIds, got: {}"),
        };

        // check that the generated instance IDs are valid
        assert!(is_valid_instance_id(fixed_entries[0].instance_id.as_ref().unwrap()));
        assert!(is_valid_instance_id(fixed_entries[1].instance_id.as_ref().unwrap()));

        // match everything except for the generated instance IDs.
        let expected_entries = vec![index.instances[1].clone(), index.instances[3].clone()];
        fixed_entries[0].instance_id = None;
        fixed_entries[1].instance_id = None;
        assert_eq!(expected_entries, fixed_entries);
    }

    #[test]
    fn index_from_json_file() {
        let mut tmp_input_manifest = tempfile::NamedTempFile::new().unwrap();
        let mut tmp_input_index1 = tempfile::NamedTempFile::new().unwrap();
        let mut tmp_input_index2 = tempfile::NamedTempFile::new().unwrap();
        let tmp_output_manifest = tempfile::NamedTempFile::new().unwrap();
        let tmp_output_depfile = tempfile::NamedTempFile::new().unwrap();

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
        tmp_input_index1.write_all(serde_json5::to_string(&index1).unwrap().as_bytes()).unwrap();

        // write the second index file
        let index2 = gen_index(2);
        tmp_input_index2.write_all(serde_json5::to_string(&index2).unwrap().as_bytes()).unwrap();

        assert!(matches!(
            run(CommandLineOpts {
                input_manifest: tmp_input_manifest.path().to_path_buf(),
                output_file: tmp_output_manifest.path().to_path_buf(),
                depfile: tmp_output_depfile.path().to_path_buf(),
            }),
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

        // assert the structure of the dependency file:
        //  <output_manifest>: <input index 1> <input index 2>\n
        assert_eq!(
            format!(
                "{}: {} {}\n",
                tmp_output_manifest.path().to_str().unwrap(),
                tmp_input_index1.path().to_str().unwrap(),
                tmp_input_index2.path().to_str().unwrap()
            ),
            fs::read_to_string(tmp_output_depfile.path()).unwrap()
        )
    }
}

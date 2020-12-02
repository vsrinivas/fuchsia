// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};

use component_id_index::*;
use fidl::encoding::encode_persistent;
use fidl_fuchsia_component_internal as fcomponent_internal;
use serde_json;
use serde_json5;
use std::convert::TryInto;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Validate and merge component ID index files.")]
struct CommandLineOpts {
    #[structopt(
        long,
        help = "Path to a manifest text file containing a list of index files, one on each line. All index files are merged into a single index, written to the supplied --output_index_json and --output_index_fidl"
    )]
    input_manifest: PathBuf,

    #[structopt(long, help = "Where to write the merged index file, encoded in JSON.")]
    output_index_json: PathBuf,

    #[structopt(long, help = "Where to write the merged index file, encoded in FIDL wire-format.")]
    output_index_fidl: PathBuf,

    #[structopt(
        short,
        long,
        help = "Where to write a dep file (.d) file of indices from --input_manifest."
    )]
    depfile: PathBuf,
}

// Make an Index using a set of JSON5-encoded index files.
fn merge_index_from_json5_files(index_files: &[String]) -> anyhow::Result<Index> {
    Index::from_files_with_decoder(index_files, |json5| {
        serde_json5::from_str(json5).context("Unable to parse JSON5")
    }).map_err(|e|{
        match e.downcast_ref::<ValidationError>() {
            Some(ValidationError::MissingInstanceIds{entries}) => {
                let corrected_entries = generate_instance_ids(&entries);
                anyhow!("Some entries are missing `instance_id` fields. Here are some generated IDs for you:\n{}\n\nSee https://fuchsia.dev/fuchsia-src/development/components/component_id_index#defining_an_index for more details.",
                            serde_json::to_string_pretty(&corrected_entries).unwrap())
            },
            _ => e
        }
    })
}

fn generate_instance_ids(entries: &Vec<InstanceIdEntry>) -> Vec<InstanceIdEntry> {
    let rng = &mut rand::thread_rng();
    (0..entries.len())
        .map(|i| {
            let mut with_id = entries[i].clone();
            with_id.instance_id = Some(gen_instance_id(rng));
            with_id
        })
        .collect::<Vec<InstanceIdEntry>>()
}

fn run(opts: CommandLineOpts) -> anyhow::Result<()> {
    let input_manifest =
        fs::File::open(opts.input_manifest).context("Could not open input manifest")?;
    let input_files = BufReader::new(input_manifest)
        .lines()
        .collect::<Result<Vec<String>, _>>()
        .context("Could not read input manifest")?;
    let merged_index = merge_index_from_json5_files(input_files.as_slice())?;

    let serialized_output_json =
        serde_json::to_string(&merged_index).context("Could not json-encode merged index")?;
    fs::write(&opts.output_index_json, serialized_output_json.as_bytes())
        .context("Could not write merged JSON-encoded index to file")?;

    let mut merged_index_fidl: fcomponent_internal::ComponentIdIndex = merged_index.try_into()?;
    let serialized_output_fidl =
        encode_persistent(&mut merged_index_fidl).context("Could not fidl-encode merged index")?;
    fs::write(&opts.output_index_fidl, serialized_output_fidl)
        .context("Could not write merged FIDL-encoded index to file")?;

    // write out the depfile
    fs::write(
        &opts.depfile,
        format!(
            "{}: {}\n{}: {}\n",
            opts.output_index_json.to_str().unwrap(),
            input_files.join(" "),
            opts.output_index_fidl.to_str().unwrap(),
            input_files.join(" ")
        ),
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
    use pretty_assertions::assert_eq;
    use regex;
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
                        transitional_realm_paths: None,
                    },
                })
                .collect(),
        }
    }

    fn make_index_file(index: &Index) -> anyhow::Result<tempfile::NamedTempFile> {
        let mut index_file = tempfile::NamedTempFile::new()?;
        index_file.write_all(serde_json::to_string(index).unwrap().as_bytes())?;
        Ok(index_file)
    }

    #[test]
    fn error_missing_instance_ids() {
        let mut index = gen_index(4);
        index.instances[1].instance_id = None;
        index.instances[3].instance_id = None;

        let index_file = make_index_file(&index).unwrap();
        let index_files = [String::from(index_file.path().to_str().unwrap())];

        // this should be an error, since `index` has entries with a missing instance ID.
        // check the error output's message as well.
        let merge_result = merge_index_from_json5_files(&index_files);
        let actual_output = merge_result.err().unwrap().to_string();
        let expected_output = r#"Some entries are missing `instance_id` fields. Here are some generated IDs for you:
[
  {
    "instance_id": "RANDOM_GENERATED_INSTANCE_ID",
    "appmgr_moniker": {
      "url": "fuchsia-pkg://example.com/fake_pkg#meta/fake_component_1.cmx",
      "realm_path": [
        "root",
        "child",
        "1"
      ],
      "transitional_realm_paths": null
    }
  },
  {
    "instance_id": "RANDOM_GENERATED_INSTANCE_ID",
    "appmgr_moniker": {
      "url": "fuchsia-pkg://example.com/fake_pkg#meta/fake_component_3.cmx",
      "realm_path": [
        "root",
        "child",
        "3"
      ],
      "transitional_realm_paths": null
    }
  }
]

See https://fuchsia.dev/fuchsia-src/development/components/component_id_index#defining_an_index for more details."#;

        let re = regex::Regex::new("[0-9a-f]{64}").unwrap();
        let actual_output_modified = re.replace_all(&actual_output, "RANDOM_GENERATED_INSTANCE_ID");
        assert_eq!(actual_output_modified, expected_output);
    }

    #[test]
    fn index_from_json5() {
        let mut index_file = tempfile::NamedTempFile::new().unwrap();
        index_file
            .write_all(
                r#"{
            // Here is a comment.
            instances: []
        }"#
                .as_bytes(),
            )
            .unwrap();

        // only checking that we parsed successfully.
        let files = [String::from(index_file.path().to_str().unwrap())];
        assert!(merge_index_from_json5_files(&files).is_ok());
    }

    #[test]
    fn multiple_indices_in_manifest() {
        let mut tmp_input_manifest = tempfile::NamedTempFile::new().unwrap();
        let mut tmp_input_index1 = tempfile::NamedTempFile::new().unwrap();
        let mut tmp_input_index2 = tempfile::NamedTempFile::new().unwrap();
        let tmp_output_index_json = tempfile::NamedTempFile::new().unwrap();
        let tmp_output_index_fidl = tempfile::NamedTempFile::new().unwrap();
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
                output_index_json: tmp_output_index_json.path().to_path_buf(),
                output_index_fidl: tmp_output_index_fidl.path().to_path_buf(),
                depfile: tmp_output_depfile.path().to_path_buf(),
            }),
            Ok(_)
        ));

        // assert that the output index file contains the merged index.
        let mut merged_index = index1.clone();
        merged_index.instances.extend_from_slice(&index2.instances);
        let index_files = [String::from(tmp_output_index_json.path().to_str().unwrap())];
        assert_eq!(merged_index, merge_index_from_json5_files(&index_files).unwrap());

        // assert the structure of the dependency file:
        //  <merged_output_index>: <input index 1> <input index 2>\n
        assert_eq!(
            format!(
                "{}: {} {}\n{}: {} {}\n",
                tmp_output_index_json.path().to_str().unwrap(),
                tmp_input_index1.path().to_str().unwrap(),
                tmp_input_index2.path().to_str().unwrap(),
                tmp_output_index_fidl.path().to_str().unwrap(),
                tmp_input_index1.path().to_str().unwrap(),
                tmp_input_index2.path().to_str().unwrap()
            ),
            fs::read_to_string(tmp_output_depfile.path()).unwrap()
        )
    }
}

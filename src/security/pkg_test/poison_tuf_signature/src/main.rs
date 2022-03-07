// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    argh::from_env,
    serde::{Deserialize, Serialize},
    serde_json::{from_reader, to_vec},
    std::{
        collections::BTreeMap,
        fs::File,
        io::{Read, Write},
    },
    zip::{
        read::ZipArchive,
        write::{FileOptions, ZipWriter},
    },
};

/// Flags for poison_tuf_signature.
#[derive(argh::FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to input zip file containing TUF repository.
    #[argh(option)]
    pub input: String,

    /// absolute path to output zip file that will contain TUF repository with
    /// poisoned signature.
    #[argh(option)]
    pub output: String,
}

#[derive(Deserialize, Serialize)]
struct TargetsJson {
    signatures: Vec<Signature>,
    signed: Targets,
}

#[derive(Deserialize, Serialize)]
struct Signature {
    keyid: String,
    sig: String,
}

#[derive(Deserialize, Serialize)]
struct Targets {
    targets: BTreeMap<String, Target>,
}

#[derive(Deserialize, Serialize)]
struct Target {
    custom: TargetCustom,
}

#[derive(Deserialize, Serialize)]
struct TargetCustom {
    merkle: String,
    size: usize,
}

fn poison_signature(targets_json_contents: &[u8]) -> Result<Vec<u8>> {
    let mut targets_json: TargetsJson =
        from_reader(targets_json_contents).context("Failed to deserialize targets.json data")?;
    if targets_json.signatures.len() == 0 {
        bail!("targets.json contains no signatures to poison");
    }
    match targets_json.signatures[0].sig.chars().next() {
        Some(first_char) => {
            targets_json.signatures[0]
                .sig
                .replace_range(0..1, if first_char == '0' { "1" } else { "0" });
            to_vec(&targets_json)
                .context("Failed to serialize targets.json with poisoned signature")
        }
        None => {
            bail!("targets.json contains empty signature hex string");
        }
    }
}

fn main() -> Result<()> {
    let _args @ Args { input, output } = &from_env();

    let input_file = File::open(input).context("Failed to open input zip archive")?;
    let mut input_zip = ZipArchive::new(input_file).context("Failed open file as zip archive")?;
    let output_file = File::create(output).context("Failed to create output zip archive")?;
    let mut output_zip = ZipWriter::new(output_file);

    for i in 0..input_zip.len() {
        let mut in_file =
            input_zip.by_index(i).context("Failed to open file inside input zip archive")?;

        output_zip
            .start_file(in_file.name(), FileOptions::default())
            .context("Failed to create file in output zip archive")?;
        let mut contents = vec![];
        in_file
            .read_to_end(&mut contents)
            .context("Failed to read file inside input zip archive")?;

        if in_file.name().ends_with("targets.json") {
            contents =
                poison_signature(contents.as_slice()).context("Failed to poison signature")?;
        }

        output_zip
            .write_all(contents.as_slice())
            .context("Failed to write to file inside output zip archive")?;
    }

    let mut output_file = output_zip.finish().context("Failed to finalize output zip archive")?;
    output_file.flush().context("Failed to flush output zip archive buffer")?;

    Ok(())
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    serde::{Deserialize, Serialize},
    std::{
        fs::File,
        io::{copy, BufReader, BufWriter, Write as _},
    },
    zip::{
        read::ZipArchive,
        write::{FileOptions, ZipWriter},
        CompressionMethod,
    },
};

/// Flags for poison_tuf_signature.
#[derive(argh::FromArgs)]
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
#[serde(deny_unknown_fields)]
struct TargetsJson {
    signatures: Vec<Signature>,
    signed: serde_json::Value,
}

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct Signature {
    sig: String,
    keyid: serde_json::Value,
}

fn main() -> anyhow::Result<()> {
    let Args { input, output } = argh::from_env();

    let input_file = File::open(input).context("Failed to open input zip archive")?;
    let input_file = BufReader::new(input_file);
    let mut input_zip = ZipArchive::new(input_file).context("Failed open file as zip archive")?;
    let output_file = File::create(output).context("Failed to create output zip archive")?;
    let output_file = BufWriter::new(output_file);
    let mut output_zip = ZipWriter::new(output_file);

    for i in 0..input_zip.len() {
        let mut in_file =
            input_zip.by_index(i).context("Failed to open file inside input zip archive")?;

        let name = in_file.name();

        // Do not compress; we are producing test-only output here and size doesn't matter.
        let options = FileOptions::default().compression_method(CompressionMethod::Stored);

        output_zip
            .start_file(name, options)
            .context("Failed to create file in output zip archive")?;

        if name.ends_with("targets.json") {
            let mut json = serde_json::from_reader(in_file)
                .context("Failed to deserialize targets.json data")?;
            let TargetsJson { signatures, signed: _ } = &mut json;
            match signatures.as_mut_slice() {
                [] => anyhow::bail!("targets.json contains no signatures to poison"),
                [Signature { keyid: _, sig }, ..] => match sig.chars().next() {
                    None => {
                        anyhow::bail!("targets.json contains no signatures to poison");
                    }
                    Some(first_char) => {
                        sig.replace_range(..1, if first_char == '0' { "1" } else { "0" });
                    }
                },
            }
            let () = serde_json::to_writer(&mut output_zip, &json)
                .context("Failed to serialize targets.json with poisoned signature")?;
        } else {
            let _: u64 = copy(&mut in_file, &mut output_zip)
                .context("Failed to write to file inside output zip archive")?;
        };
    }

    let mut output_file = output_zip.finish().context("Failed to finalize output zip archive")?;
    output_file.flush().context("Failed to flush output zip archive buffer")?;

    Ok(())
}

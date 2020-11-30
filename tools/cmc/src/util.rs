// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    serde_json::Value,
    serde_json5,
    std::{
        convert::TryInto,
        fs,
        io::{Read, Write},
        path::PathBuf,
    },
};

/// Read a JSON or JSON5 file.
/// Attempts to parse as JSON first.
/// If this fails, attempts to parse as JSON5.
/// Parsing with serde_json5 is known to be much slower, so we try the faster
/// parser first.
pub fn json_or_json5_from_file(file: &PathBuf) -> Result<Value, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;

    serde_json::from_str(&buffer).or_else(|_| {
        // If JSON parsing fails, try JSON5 parsing (which is slower)
        serde_json5::from_str(&buffer).map_err(|e| {
            Error::parse(
                format!("Couldn't read {:#?} as JSON: {}", file, e),
                e.try_into().ok(),
                Some(file.as_path()),
            )
        })
    })
}

/// Write a depfile.
/// Given an output and its includes, writes a depfile in Make format.
/// If there is no output, deletes the potentially stale depfile.
pub fn write_depfile(
    depfile: &PathBuf,
    output: Option<&PathBuf>,
    includes: &Vec<String>,
    includepath: &PathBuf,
) -> Result<(), Error> {
    if output.is_none() || includes.is_empty() {
        // A non-existent depfile is the same as an empty depfile
        if depfile.exists() {
            // Delete stale depfile
            fs::remove_file(depfile)?;
        }
    } else if let Some(output_path) = output {
        let depfile_contents = format!("{}:", output_path.display())
            + &includes
                .iter()
                .map(|i| format!(" {}", includepath.join(i).display()))
                .collect::<String>()
            + "\n";
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(depfile)?
            .write_all(depfile_contents.as_bytes())?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::Read;
    use tempfile::TempDir;

    #[test]
    fn test_write_depfile() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_path = tmp_dir.path();
        let depfile = tmp_path.join("foo.d");
        let output = tmp_path.join("foo.cml");
        let includes = vec!["bar.cml".to_string(), "qux.cml".to_string()];
        write_depfile(&depfile, Some(&output), &includes, &tmp_path.to_path_buf()).unwrap();

        let mut depfile_contents = String::new();
        File::open(&depfile).unwrap().read_to_string(&mut depfile_contents).unwrap();
        assert_eq!(
            depfile_contents,
            format!("{tmp}/foo.cml: {tmp}/bar.cml {tmp}/qux.cml\n", tmp = tmp_path.display())
        );
    }
}

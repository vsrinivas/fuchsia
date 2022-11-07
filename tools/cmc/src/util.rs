// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        cml,
        error::{Error, Location},
    },
    cm_rust::ComponentDecl,
    fidl::encoding::decode_persistent,
    fidl_fuchsia_component_decl::Component,
    serde_json::Value,
    serde_json5,
    std::{
        convert::{TryFrom, TryInto},
        fs,
        io::{Read, Write},
        path::{Path, PathBuf},
    },
};

/// Read a JSON or JSON5 file.
/// Attempts to parse as JSON first.
/// If this fails, attempts to parse as JSON5.
/// Parsing with serde_json5 is known to be much slower, so we try the faster
/// parser first.
pub fn json_or_json5_from_file(file: &PathBuf) -> Result<Value, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)
        .map_err(|e| {
            Error::invalid_args(format!("Could not open file at path \"{:?}\": {}", file, e))
        })?
        .read_to_string(&mut buffer)?;

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
    inputs: &Vec<PathBuf>,
) -> Result<(), Error> {
    if output.is_none() {
        // A non-existent depfile is the same as an empty depfile
        if depfile.exists() {
            // Delete stale depfile
            fs::remove_file(depfile)?;
        }
    } else if let Some(output_path) = output {
        let depfile_contents = format!(
            "{}:{}\n",
            output_path.display(),
            &inputs.iter().map(|i| format!(" {}", i.display())).collect::<String>()
        );
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(depfile)?
            .write_all(depfile_contents.as_bytes())?;
    }
    Ok(())
}

/// Read .cmx file and parse into JSON object.
pub fn read_cmx(file: &Path) -> Result<serde_json::Value, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;
    serde_json::from_str(&buffer).map_err(|e| {
        Error::parse(format!("Couldn't parse file {:?}: {}", file, e), None, Some(file))
    })
}

/// Read .cml file and parse into a cml::Document.
pub fn read_cml(file: &Path) -> Result<cml::Document, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)
        .map_err(|e| {
            Error::parse(format!("Couldn't read include {:?}: {}", file, e), None, Some(file))
        })?
        .read_to_string(&mut buffer)
        .map_err(|e| {
            Error::parse(format!("Couldn't read include {:?}: {}", file, e), None, Some(file))
        })?;

    serde_json5::from_str(&buffer).map_err(|e| {
        let serde_json5::Error::Message { location, msg } = e;
        let location = location.map(|l| Location { line: l.line, column: l.column });
        Error::parse(msg, location, Some(file))
    })
}

/// Read .cm file and parse into a cm_rust::ComponentDecl.
pub fn read_cm(file: &Path) -> Result<ComponentDecl, Error> {
    let mut buffer = vec![];
    fs::File::open(&file)
        .map_err(|e| Error::parse(format!("Couldn't open file: {}", e), None, Some(file)))?
        .read_to_end(&mut buffer)
        .map_err(|e| Error::parse(format!("Couldn't read file: {}", e), None, Some(file)))?;
    let fidl_component_decl: Component = decode_persistent(&buffer).map_err(|e| {
        Error::parse(format!("Couldn't decode bytes to Component FIDL: {}", e), None, Some(file))
    })?;
    ComponentDecl::try_from(fidl_component_decl).map_err(|e| {
        Error::parse(format!("Couldn't convert Component FIDL to cm_rust: {}", e), None, Some(file))
    })
}

pub fn ensure_directory_exists(output: &PathBuf) -> Result<(), Error> {
    if let Some(parent) = output.parent() {
        if !parent.exists() {
            std::fs::create_dir_all(parent)?;
        }
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
        let includes = vec![tmp_path.join("bar.cml"), tmp_path.join("qux.cml")];
        write_depfile(&depfile, Some(&output), &includes).unwrap();

        let mut depfile_contents = String::new();
        File::open(&depfile).unwrap().read_to_string(&mut depfile_contents).unwrap();
        assert_eq!(
            depfile_contents,
            format!("{tmp}/foo.cml: {tmp}/bar.cml {tmp}/qux.cml\n", tmp = tmp_path.display())
        );
    }

    #[test]
    fn test_write_depfile_no_includes() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_path = tmp_dir.path();
        let depfile = tmp_path.join("foo.d");
        let output = tmp_path.join("foo.cml");
        let includes = vec![];
        write_depfile(&depfile, Some(&output), &includes).unwrap();

        let mut depfile_contents = String::new();
        File::open(&depfile).unwrap().read_to_string(&mut depfile_contents).unwrap();
        assert_eq!(depfile_contents, format!("{tmp}/foo.cml:\n", tmp = tmp_path.display()));
    }

    #[test]
    fn test_ensure_directory_exists() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_path = tmp_dir.path();
        let nested_directory = tmp_path.join("foo/bar");
        let nested_file = nested_directory.join("qux.cml");
        assert!(!nested_directory.exists());
        ensure_directory_exists(&nested_file).unwrap();
        assert!(nested_directory.exists());
        // Operation is idempotent
        ensure_directory_exists(&nested_file).unwrap();
        assert!(nested_directory.exists());
    }
}

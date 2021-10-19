// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    byteorder::{BigEndian, ByteOrder, WriteBytesExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{Inspector, Node},
    futures::stream::StreamExt,
    lazy_static::lazy_static,
    std::{
        fs,
        io::Read,
        path::{Path, PathBuf},
    },
    tracing::{info, warn},
};

const U64_SIZE: usize = (u64::BITS / 8) as usize;
const DEFAULT_VALUE: u64 = 1;

lazy_static! {
    static ref FILE_PATH: PathBuf = PathBuf::from("/data/counter");
    static ref INSPECTOR: Inspector = Inspector::new();
}

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), Error> {
    info!("Initializing and serving inspect on servicefs");
    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&INSPECTOR, &mut fs)?;

    info!("Attempted to read and write from storage");
    process_file(&FILE_PATH, INSPECTOR.root());

    fs.take_and_serve_directory_handle()?;
    Ok(fs.collect().await)
}

/// Attempts to read a single u64 from the supplied path then write an incremented value back into
/// the same path. If any errors are encountered during reading the write will use a default
/// value.
fn process_file(path: &Path, inspect_node: &Node) {
    let updated_value = match read_file(path) {
        Ok(value) => {
            info!("Successfully read a value of: {}", value);
            inspect_node.record_uint("read_value", value);
            value.wrapping_add(1)
        }
        Err(err) => {
            let error_string = format!("{:?}", err);
            warn!("Error reading previous value: {}", error_string);
            inspect_node.record_string("read_error", error_string);
            DEFAULT_VALUE
        }
    };

    match write_file(path, updated_value) {
        Ok(()) => {
            info!("Successfully wrote a value of: {}", updated_value);
            inspect_node.record_uint("write_value", updated_value);
        }
        Err(err) => {
            let error_string = format!("{:?}", err);
            warn!("Error writing updated value: {}", error_string);
            inspect_node.record_string("write_error", error_string);
        }
    }
}

/// Reads a single u64 from the supplied path, returning an error if the file does not exists or is
/// not exactly the right length.
fn read_file(path: &Path) -> Result<u64, Error> {
    let mut buf = Vec::<u8>::new();
    let mut file = fs::File::open(path).map_err(|err| anyhow!("Error opening file: {:?}", err))?;
    let size =
        file.read_to_end(&mut buf).map_err(|err| anyhow!("Error reading file: {:?}", err))?;
    match size {
        U64_SIZE => Ok(BigEndian::read_u64(&buf)),
        _ => Err(anyhow!("File length invalid: {}", size)),
    }
}

/// Writes the supplied u64 into the supplied path, returning any errors that are encountered.
fn write_file(path: &Path, value: u64) -> Result<(), Error> {
    let mut file =
        fs::File::create(path).map_err(|err| anyhow!("Error creating file: {:?}", err))?;
    file.write_u64::<BigEndian>(value).map_err(|err| anyhow!("Error writing value: {:?}", err))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_data_tree, AnyProperty},
        tempfile::TempDir,
        test_case::test_case,
    };

    fn make_path() -> (TempDir, PathBuf) {
        let dir = TempDir::new().expect("error creating tempdir");
        let path = dir.path().join("testfile");
        (dir, path)
    }

    fn make_file(data: Vec<u8>) -> (TempDir, PathBuf) {
        let (dir, path) = make_path();
        fs::write(&path, &data).expect("error writing file");
        (dir, path)
    }

    #[test_case(vec![0, 0, 0, 0, 1, 2, 3, 4], Some(0x01020304); "valid")]
    #[test_case(vec![0, 0, 0, 0, 1, 2, 3], None; "too short")]
    #[test_case(vec![0, 0, 0, 0, 1, 2, 3, 4, 5], None; "too long")]
    fn test_read_existing_file(content: Vec<u8>, expected: Option<u64>) {
        let (_tempdir, path) = make_file(content);
        match expected {
            Some(value) => assert_eq!(read_file(&path).unwrap(), value),
            None => assert!(read_file(&path).is_err(), "read should fail"),
        }
    }

    #[test]
    fn test_read_missing_file() {
        let (_tempdir, path) = make_path();
        assert!(read_file(&path).is_err(), "read should fail");
    }

    #[test_case(vec![0, 0, 0, 0, 1, 2, 3, 4], Some(0x01020304), 0x01020305; "valid")]
    #[test_case(vec![255, 255, 255, 255, 255, 255, 255, 255], Some(u64::MAX), 0x0; "rollover")]
    #[test_case(vec![0, 0, 0, 0, 1, 2], None, DEFAULT_VALUE; "content too short")]
    fn test_process_existing_file(content: Vec<u8>, read: Option<u64>, write: u64) {
        let (_tempdir, path) = make_file(content);
        let inspector = &Inspector::new();
        process_file(&path, inspector.root());
        assert_eq!(read_file(&path).unwrap(), write);
        match read {
            Some(read_value) => assert_data_tree!(
            inspector,
            root: contains {
                read_value: read_value,
                write_value: write,
            }),
            None => assert_data_tree!(
            inspector,
            root: contains {
                read_error: AnyProperty,
                write_value: write,
            }),
        }
    }

    #[test]
    fn test_process_missing_file() {
        let (_tempdir, path) = make_path();
        let inspector = &Inspector::new();
        process_file(&path, inspector.root());
        assert_eq!(read_file(&path).unwrap(), DEFAULT_VALUE);
        assert_data_tree!(
        inspector,
        root: contains {
            read_error: AnyProperty,
            write_value: DEFAULT_VALUE,
        });
    }

    #[test]
    fn test_process_invalid_directory() {
        let inspector = &Inspector::new();
        process_file(&PathBuf::from("/i_dont_exist"), inspector.root());
        assert_data_tree!(
        inspector,
        root: contains {
            read_error: AnyProperty,
            write_error: AnyProperty,
        });
    }
}

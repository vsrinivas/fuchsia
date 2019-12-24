// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file demonstrates how one can use the `fuchsia.sys.test.CacheControl` protocol to test
//! code that uses the `isolated-cache-storage` feature to store data on disk. This file doubles as
//! an integration test for appmgr, as the example and test are identical. Take care when changing
//! this file to not accidentally reduce test coverage of appmgr.

use std::{fs, io};

const FILE_PATH: &str = "/cache/example_file";
const FILE_CONTENTS: &str = "I was stored in isolated cache storage!";

fn main() {
    println!("file contents: \"{}\"", get_contents_of_cache_file());
}

// get_contents_of_cache_file will attempt to read `FILE_PATH` from the filesystem. If the file
// doesn't exist, it will create it and then read it. This function aims to mimic use cases for
// isolated cache storage, wherein files are stored on disk that can be recreated if deleted.
fn get_contents_of_cache_file() -> String {
    let res = fs::read_to_string(FILE_PATH);
    match res {
        Ok(msg) => msg,
        Err(e) => {
            if e.kind() != io::ErrorKind::NotFound {
                panic!("failed to read file '{}': {:?}", FILE_PATH, e);
            }
            fs::write(FILE_PATH, FILE_CONTENTS).expect("failed to write file");
            fs::read_to_string(FILE_PATH).expect("failed to read file")
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context, Error},
        fidl_fuchsia_sys_test as systest, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        std::path::{Path, PathBuf},
    };

    fn read_dir_paths<P: AsRef<Path>>(dir: P) -> Result<Vec<PathBuf>, io::Error> {
        fs::read_dir(dir)?.map(|r| r.map(|d| d.path())).collect()
    }

    #[fasync::run_singlethreaded(test)]
    async fn my_test() -> Result<(), Error> {
        // Populate the contents of the cache file, and make sure we can read it
        assert_eq!(FILE_CONTENTS, get_contents_of_cache_file());

        // A second call to this function should have the same behavior
        assert_eq!(FILE_CONTENTS, get_contents_of_cache_file());

        // Make sure the contents of /cache match what we expect
        assert_eq!(vec![PathBuf::from(FILE_PATH)], read_dir_paths("/cache")?);

        // Connect to CacheControl, clear the cache for the system
        let cache_control = connect_to_service::<systest::CacheControlMarker>()?;
        cache_control.clear().await.context("failed to clear cache")?;

        // Make sure the contents of /cache match what we expect
        assert!(read_dir_paths("/cache")?.is_empty());

        // This function should still succeed, and will repopulate the cache file
        assert_eq!(FILE_CONTENTS, get_contents_of_cache_file());

        // Make sure the contents of /cache were repopulated
        assert_eq!(vec![PathBuf::from(FILE_PATH)], read_dir_paths("/cache")?);
        Ok(())
    }
}

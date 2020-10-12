// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::handler::device_storage::DeviceStorageCompatible,
    anyhow::{format_err, Error},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    serde::de::DeserializeOwned,
    serde::Serialize,
    std::fs,
    std::io::Write,
    std::path::PathBuf,
};

// Structs that implement this can be supported by StoreAccessor. The file name
// provided should be a valid file name.
pub trait StoreAccessorCompatible:
    Serialize + DeserializeOwned + Clone + PartialEq + DeviceStorageCompatible
{
    const FILE_NAME: &'static str;
}

// This struct is used to read/write data into a file for persistent storage.
pub struct StoreAccessor<T: StoreAccessorCompatible> {
    file_path: PathBuf,
    current_data: Option<T>,
}

impl<T: StoreAccessorCompatible> StoreAccessor<T> {
    pub fn new(current_data: Option<T>, file_directory: PathBuf) -> Self {
        let mut path = file_directory.clone();
        path.push(&T::FILE_NAME);
        return StoreAccessor { file_path: path, current_data: current_data };
    }

    pub async fn get_value(&mut self) -> T {
        if self.current_data == None {
            self.current_data = Some(self.read_or_create_file().await);
        }

        self.current_data.as_ref().unwrap().clone()
    }

    async fn read_or_create_file(&mut self) -> T {
        match fs::read_to_string(&self.file_path) {
            Ok(value) => T::deserialize_from(&value),
            Err(_) => {
                fx_log_info!("store file doesn't exist, creating file");
                if let Err(e) = self.set_value(&T::default_value()).await {
                    fx_log_err!("unable to create file: {}", e);
                };
                T::default_value()
            }
        }
    }

    pub async fn set_value(&mut self, new_value: &T) -> Result<(), Error> {
        if self.current_data.as_ref() == Some(new_value) {
            return Ok(());
        }

        // Save the current data. Even if persistent storage fails, it's still saved
        // locally.
        self.current_data = Some(new_value.clone());

        // To prevent corrupted writes, create and write to a temporary file. After that,
        // replace the data file with the temporary one.
        let temp_file_path = self.file_path.with_extension("tmp");

        let mut file = match fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(temp_file_path.clone())
        {
            Ok(f) => f,
            Err(e) => {
                fx_log_err!("unable open or create {}, {}", temp_file_path.to_str().unwrap(), e);
                return Err(format_err!("unable to write to storage"));
            }
        };
        if let Err(e) = file.write_all(new_value.serialize_to().as_bytes()) {
            fx_log_err!("unable to write to file {}, {}", temp_file_path.to_str().unwrap(), e);
            return Err(format_err!("unable to write to storage"));
        }

        if let Err(e) = fs::rename(temp_file_path, &self.file_path) {
            fx_log_err!("unable to replace file with temp, {}", e);
            return Err(format_err!("unable to write to storage"));
        }

        Ok(())
    }
}

#[cfg(test)]
pub mod testing {
    use super::*;
    use serde::{Deserialize, Serialize};
    use tempfile::TempDir;

    #[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
    struct TestData {
        pub value: f32,
    }

    impl DeviceStorageCompatible for TestData {
        const KEY: &'static str = "test_key";

        fn default_value() -> Self {
            TestData { value: 0.0 }
        }
    }

    impl StoreAccessorCompatible for TestData {
        const FILE_NAME: &'static str = "test_store_data.store";
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_and_write_new_file() {
        let tmp_dir = TempDir::new().expect("should have created tempdir");

        let mut store = StoreAccessor::<TestData>::new(None, tmp_dir.path().to_path_buf());
        assert_eq!(TestData::default_value(), store.get_value().await);

        // Check to see if the file is created with the default value written to it.
        let data_file_path = tmp_dir.path().join(&TestData::FILE_NAME);
        assert!(data_file_path.exists());
        assert_eq!(
            TestData::default_value().serialize_to(),
            fs::read_to_string(&data_file_path).expect("should read in file")
        );

        // Change the value.
        let new_val = TestData { value: 5.0 };
        store.set_value(&new_val).await.expect("should set new value");
        assert_eq!(new_val, store.get_value().await);

        // Check that the file is written.
        assert_eq!(
            new_val.serialize_to(),
            fs::read_to_string(&data_file_path).expect("should read in file")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_data_from_file() {
        let mut store = StoreAccessor::<TestData>::new(None, PathBuf::from("/pkg/data/"));
        assert_eq!(TestData { value: 10.0 }, store.get_value().await);
    }
}

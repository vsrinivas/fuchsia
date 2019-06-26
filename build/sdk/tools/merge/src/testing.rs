// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::collections::HashMap;

use sdk_metadata::JsonObject;

use crate::app::{Error, Result};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl TarballContent for String {
    fn is_identical(&self, other: &String) -> Result<bool> {
        Ok(self == other)
    }
}

pub struct MockInputTarball {
    files: RefCell<HashMap<String, String>>,
}

impl MockInputTarball {
    pub fn new() -> MockInputTarball {
        MockInputTarball {
            files: RefCell::new(HashMap::new()),
        }
    }

    pub fn add(&self, path: &str, content: &str) {
        assert!(
            self.files
                .borrow_mut()
                .insert(path.to_owned(), content.to_owned())
                .is_none(),
            format!("Path already added: {}", path)
        );
    }
}

impl InputTarball<String> for MockInputTarball {
    fn get_file<R>(&self, path: &str, reader: R) -> Result<()>
    where
        R: FnOnce(&mut String) -> Result<()>,
    {
        self.files
            .borrow_mut()
            .get_mut(path)
            .ok_or_else(|| Error::ArchiveFileNotFound {
                name: path.to_owned(),
            })
            .map(|file_path| -> Result<()> { reader(file_path) })?
    }

    fn get_metadata<T: JsonObject>(&self, path: &str) -> Result<T> {
        let mut contents = String::new();
        self.get_file(path, |value| {
            contents.push_str(value);
            Ok(())
        })?;
        T::new(contents.as_bytes())
    }
}

pub struct MockOutputTarball {
    files: HashMap<String, String>,
}

impl MockOutputTarball {
    pub fn new() -> MockOutputTarball {
        MockOutputTarball {
            files: HashMap::new(),
        }
    }

    pub fn assert_has_file(&self, path: &str) {
        assert!(
            self.files.contains_key(path),
            format!("File not found: {}", path)
        );
    }

    pub fn get_content(&self, path: &str) -> &String {
        self.assert_has_file(path);
        self.files.get(path).unwrap()
    }
}

impl OutputTarball<String> for MockOutputTarball {
    fn write_json<T: JsonObject>(&mut self, path: &str, content: &T) -> Result<()> {
        self.write_file(path, &mut content.to_string().unwrap())
    }

    fn write_file(&mut self, path: &str, file: &mut String) -> Result<()> {
        assert!(self.files.insert(path.to_owned(), file.clone()).is_none());
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_input_get() {
        let tarball = MockInputTarball::new();
        let path = "foo/bar.json";
        let content = "hello world";
        tarball.add(path, content);
        tarball
            .get_file(path, |file_content| {
                assert_eq!(file_content, content);
                Ok(())
            })
            .unwrap();
    }

    #[test]
    fn test_input_get_nonexistent() {
        let tarball = MockInputTarball::new();
        let path = "foo/bar.json";
        assert!(tarball
            .get_file(path, |_file_content| {
                assert!(false, "Should not have been called");
                Ok(())
            })
            .is_err());
    }

    #[test]
    #[should_panic]
    fn test_input_double_add() {
        let tarball = MockInputTarball::new();
        let path = "foo/bar.json";
        let content = "hello world";
        tarball.add(path, content);
        tarball.add(path, content);
    }

    #[test]
    fn test_output_assert() {
        let mut tarball = MockOutputTarball::new();
        let path = "foo/bar.json";
        let mut content = "hello world".to_owned();
        tarball.write_file(path, &mut content).unwrap();
        tarball.assert_has_file(path);
    }

    #[test]
    #[should_panic]
    fn test_output_assert_nonexistent() {
        let tarball = MockOutputTarball::new();
        let path = "foo/bar.json";
        tarball.assert_has_file(path);
    }

    #[test]
    #[should_panic]
    fn test_output_double_write() {
        let mut tarball = MockOutputTarball::new();
        let path = "foo/bar.json";
        let mut content = "hello world".to_owned();
        tarball.write_file(path, &mut content).unwrap();
        tarball.write_file(path, &mut content).unwrap();
    }
}

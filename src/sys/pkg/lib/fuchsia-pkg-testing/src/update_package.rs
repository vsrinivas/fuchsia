// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {serde_json::json, update_package::UpdatePackage};

/// A mocked update package for testing.
pub struct TestUpdatePackage {
    update_pkg: UpdatePackage,
    temp_dir: tempfile::TempDir,
    packages: Vec<String>,
}

impl TestUpdatePackage {
    /// Creates a new TestUpdatePackage with nothing in it.
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        let temp_dir = tempfile::tempdir().expect("/tmp to exist");
        let update_pkg_proxy = io_util::directory::open_in_namespace(
            temp_dir.path().to_str().unwrap(),
            io_util::OPEN_RIGHT_READABLE,
        )
        .expect("temp dir to open");
        Self { temp_dir, update_pkg: UpdatePackage::new(update_pkg_proxy), packages: vec![] }
    }

    /// Adds a file to the update package, panics on error.
    pub async fn add_file(
        self,
        path: impl AsRef<std::path::Path>,
        contents: impl AsRef<[u8]>,
    ) -> Self {
        let path = path.as_ref();
        match path.parent() {
            Some(empty) if empty == std::path::Path::new("") => {}
            None => {}
            Some(parent) => std::fs::create_dir_all(self.temp_dir.path().join(parent)).unwrap(),
        }
        io_util::file::write_in_namespace(
            self.temp_dir.path().join(path).to_str().unwrap(),
            contents,
        )
        .await
        .expect("create test update package file");
        self
    }

    /// Adds a package to the update package, panics on error.
    pub async fn add_package(mut self, package_url: impl Into<String>) -> Self {
        self.packages.push(package_url.into());
        let packages_json = json!({
            "version": "1",
            "content": self.packages,
        })
        .to_string();
        self.add_file("packages.json", packages_json).await
    }

    /// Set the hash of the update package, panics on error.
    pub async fn hash(self, hash: impl AsRef<[u8]>) -> Self {
        self.add_file("meta", hash).await
    }
}

impl std::ops::Deref for TestUpdatePackage {
    type Target = UpdatePackage;

    fn deref(&self) -> &Self::Target {
        &self.update_pkg
    }
}

/// Provided a list of strings representing fuchsia-pkg URLs, constructs
/// a `packages.json` representing those packages and returns the JSON as a
/// string.
pub fn make_packages_json<'a>(urls: impl AsRef<[&'a str]>) -> String {
    json!({
      "version": "1",
      "content": urls.as_ref(),
    })
    .to_string()
}

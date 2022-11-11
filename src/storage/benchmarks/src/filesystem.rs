// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::BlockDeviceFactory,
    async_trait::async_trait,
    std::path::{Path, PathBuf},
};

/// A trait for creating `Filesystem`s.
#[async_trait]
pub trait FilesystemConfig {
    /// Starts an instance of the filesystem.
    async fn start_filesystem(
        &self,
        block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem>;

    /// The name of the filesystem. This is used for filtering benchmarks and outputting results.
    fn name(&self) -> String;
}

/// A trait representing a mounted filesystem that benchmarks will be run against.
#[async_trait]
pub trait Filesystem: Send {
    /// Clears all cached files in the filesystem. This method is used in "cold" benchmarks to
    /// ensure that the filesystem isn't using cached data from the setup phase in the benchmark
    /// phase.
    async fn clear_cache(&mut self);

    /// Cleans up the filesystem after a benchmark has run.
    async fn shutdown(self: Box<Self>);

    /// Path to where the filesystem is located in the current process's namespace. All benchmark
    /// operations should happen within this directory.
    fn benchmark_dir(&self) -> &Path;
}

/// A `FilesystemConfig` for a filesystem that is already present in the process's namespace.
pub struct MountedFilesystem {
    /// Path to an existing filesystem.
    dir: PathBuf,
}

impl MountedFilesystem {
    pub fn new<P: Into<PathBuf>>(dir: P) -> Self {
        Self { dir: dir.into() }
    }
}

#[async_trait]
impl FilesystemConfig for MountedFilesystem {
    async fn start_filesystem(
        &self,
        _block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem> {
        // Create a new directory within the existing filesystem to make it easier for cleaning up
        // afterwards.
        let path = self.dir.join("benchmark");
        std::fs::create_dir(&path).unwrap_or_else(|e| {
            panic!("failed to created benchmark directory '{}': {:?}", path.display(), e)
        });
        Box::new(MountedFilesystemInstance::new(path))
    }

    fn name(&self) -> String {
        self.dir.to_str().unwrap().to_owned()
    }
}

/// A `Filesystem` instance for a filesystem that is already present in the process's namespace.
pub struct MountedFilesystemInstance {
    dir: PathBuf,
}

impl MountedFilesystemInstance {
    pub fn new<P: Into<PathBuf>>(dir: P) -> Self {
        Self { dir: dir.into() }
    }
}

#[async_trait]
impl Filesystem for MountedFilesystemInstance {
    async fn clear_cache(&mut self) {
        panic!("MountedFilesystem can't be used with benchmarks that require clearing the cache");
    }

    async fn shutdown(self: Box<Self>) {
        std::fs::remove_dir_all(self.dir).expect("Failed to remove benchmark directory");
    }

    fn benchmark_dir(&self) -> &Path {
        self.dir.as_path()
    }
}

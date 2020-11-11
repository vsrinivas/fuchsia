// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{spawn_etc, transfer_fd, SpawnAction, SpawnOptions},
    fuchsia_zircon as zx,
    std::{ffi::CString, fs::File},
};

#[derive(Default)]
/// Convience wrapper for `spawn_etc`.
pub struct SpawnBuilder {
    options: Option<SpawnOptions>,
    args: Vec<CString>,
    dirs: (Vec<CString>, Vec<zx::Handle>),
}

impl SpawnBuilder {
    /// Create a `SpawnBuilder` with empty `SpawnOptions`.
    pub fn new() -> Self {
        Self::default()
    }

    /// Set the `SpawnOptions`.
    pub fn options(mut self, options: SpawnOptions) -> Self {
        self.options = Some(options);
        self
    }

    /// Add an argument.
    pub fn arg(self, arg: impl Into<String>) -> Result<Self, Error> {
        self.arg_impl(arg.into())
    }

    fn arg_impl(mut self, arg: String) -> Result<Self, Error> {
        self.args.push(CString::new(arg).map_err(Error::ConvertArgToCString)?);
        Ok(self)
    }

    /// Add a directory that will be added to the spawned process's namespace.
    pub fn add_dir_to_namespace(self, path: impl Into<String>, dir: File) -> Result<Self, Error> {
        self.add_dir_to_namespace_impl(path.into(), dir)
    }

    fn add_dir_to_namespace_impl(mut self, path: String, dir: File) -> Result<Self, Error> {
        self.dirs.0.push(CString::new(path).map_err(Error::ConvertNamespacePathToCString)?);
        self.dirs.1.push(transfer_fd(dir).map_err(Error::TransferFd)?);
        Ok(self)
    }

    /// Spawn a process from the binary located at `path`.
    pub fn spawn_from_path(
        self,
        path: impl Into<String>,
        job: &zx::Job,
    ) -> Result<zx::Process, Error> {
        self.spawn_from_path_impl(path.into(), job)
    }

    pub fn spawn_from_path_impl(self, path: String, job: &zx::Job) -> Result<zx::Process, Error> {
        let mut actions = vec![];
        for (path, handle) in itertools::zip(&self.dirs.0, self.dirs.1.into_iter()) {
            actions.push(SpawnAction::add_namespace_entry(&path, handle));
        }

        spawn_etc(
            job,
            self.options.unwrap_or(SpawnOptions::empty()),
            &CString::new(path).map_err(Error::ConvertBinaryPathToCString)?,
            self.args.iter().map(|arg| arg.as_ref()).collect::<Vec<_>>().as_slice(),
            None,
            actions.as_mut_slice(),
        )
        .map_err(|(status, message)| Error::Spawn { status, message })
    }
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("failed to convert process argument to CString")]
    ConvertArgToCString(#[source] std::ffi::NulError),

    #[error("failed to convert namespace path to CString")]
    ConvertNamespacePathToCString(#[source] std::ffi::NulError),

    #[error("failed to convert binary path to CString")]
    ConvertBinaryPathToCString(#[source] std::ffi::NulError),

    #[error("failed to transfer_fd")]
    TransferFd(#[source] zx::Status),

    #[error("spawn failed with status: {status} and message: {message}")]
    Spawn { status: zx::Status, message: String },
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::AsHandleRef as _,
        fuchsia_async as fasync,
        std::{fs::File, io::Write as _},
    };

    async fn process_exit_success(proc: zx::Process) {
        assert_eq!(
            fasync::OnSignals::new(&proc.as_handle_ref(), zx::Signals::PROCESS_TERMINATED)
                .await
                .unwrap(),
            zx::Signals::PROCESS_TERMINATED
        );
        assert_eq!(proc.info().expect("process info").return_code, 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn spawn_builder() {
        let tempdir = tempfile::TempDir::new().unwrap();
        let () = File::create(tempdir.path().join("injected-file"))
            .unwrap()
            .write_all("some-contents".as_bytes())
            .unwrap();
        let dir = File::open(tempdir.path()).unwrap();

        let builder = SpawnBuilder::new()
            .options(SpawnOptions::DEFAULT_LOADER)
            .arg("arg0")
            .unwrap()
            .arg("arg1")
            .unwrap()
            .add_dir_to_namespace("/injected-dir", dir)
            .unwrap();
        let process = builder
            .spawn_from_path("/pkg/bin/spawn_builder_test_target", &fuchsia_runtime::job_default())
            .unwrap();

        process_exit_success(process).await;
    }
}

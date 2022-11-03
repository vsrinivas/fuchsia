// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxr/66157): Migrate to the new library that substitutes io_utils and fuchsia_fs::directory.
// Ask for host-side support on the new library (fxr/467217).

use {
    anyhow::{format_err, Error, Result},
    fidl::endpoints::{create_endpoints, ClientEnd},
    fidl_fuchsia_io as fio,
    fuchsia_fs::directory::readdir,
    fuchsia_fs::directory::DirEntry,
    fuchsia_fs::{
        directory::{
            clone_no_describe, open_directory, open_directory_no_describe, open_file_no_describe,
        },
        file::{close, read, read_to_string, write},
    },
    fuchsia_zircon_status::Status,
    futures::lock::Mutex,
    std::path::{Path, PathBuf},
};

// A convenience wrapper over a FIDL DirectoryProxy.
pub struct Directory {
    path: PathBuf,
    proxy: fio::DirectoryProxy,
    // The `fuchsia.io.Directory` protocol is stateful in readdir, and the associated `fuchsia_fs::directory`
    // library used for enumerating the directory has no mechanism for synchronization of readdir
    // operations, as such this mutex must be held throughout directory enumeration in order to
    // avoid race conditions from concurrent rewinds and reads.
    readdir_mutex: Mutex<()>,
}

impl Directory {
    // Connect to a directory in the namespace
    #[cfg(target_os = "fuchsia")]
    pub fn from_namespace<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path_str = path
            .as_ref()
            .as_os_str()
            .to_str()
            .ok_or_else(|| format_err!("could not convert path to string"))?;
        let proxy =
            fuchsia_fs::directory::open_in_namespace(path_str, fio::OpenFlags::RIGHT_READABLE)?;
        let path = path.as_ref().to_path_buf();
        Ok(Self { path, proxy, readdir_mutex: Mutex::new(()) })
    }

    // Return a list of directory entries in the directory.
    pub async fn entries(&self) -> Result<Vec<DirEntry>, Error> {
        let _lock = self.readdir_mutex.lock().await;
        match readdir(&self.proxy).await {
            Ok(entries) => Ok(entries),
            Err(e) => Err(format_err!(
                "could not get entries of `{}`: {}",
                self.path.as_path().display(),
                e
            )),
        }
    }
    // Create a Directory object from a proxy.
    pub fn from_proxy(proxy: fio::DirectoryProxy) -> Self {
        let path = PathBuf::from(".");
        Self { path, proxy, readdir_mutex: Mutex::new(()) }
    }

    // Open a directory at the given `relative_path` as readable.
    pub fn open_dir_readable<P: AsRef<Path>>(&self, relative_path: P) -> Result<Self> {
        self.open_dir(relative_path, fio::OpenFlags::RIGHT_READABLE)
    }

    // Open a directory at the given `relative_path` with the provided flags.
    pub fn open_dir<P: AsRef<Path>>(
        &self,
        relative_path: P,
        flags: fio::OpenFlags,
    ) -> Result<Self> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("could not convert relative path to &str")),
        };
        match open_directory_no_describe(&self.proxy, relative_path, flags) {
            Ok(proxy) => Ok(Self { path, proxy, readdir_mutex: Mutex::new(()) }),
            Err(e) => Err(format_err!("could not open dir `{}`: {}", path.as_path().display(), e)),
        }
    }

    pub async fn verify_directory_is_read_write<P: AsRef<Path>>(
        &self,
        relative_path: P,
    ) -> Result<()> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("relative path is not valid unicode")),
        };

        let directory = self.clone_proxy()?;

        match open_directory(&directory, relative_path, fio::OpenFlags::RIGHT_WRITABLE).await {
            Ok(_) => Ok(()),
            Err(e) => {
                return Err(format_err!(
                    "Not read write directory! {}, {}",
                    path.as_path().display(),
                    e
                ))
            }
        }
    }

    // Read the contents of a file at the given `relative_path` as a string.
    pub async fn read_file<P: AsRef<Path>>(&self, relative_path: P) -> Result<String> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("relative path is not valid unicode")),
        };

        let proxy =
            match open_file_no_describe(&self.proxy, relative_path, fio::OpenFlags::RIGHT_READABLE)
            {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!(
                        "could not open file `{}`: {}",
                        path.as_path().display(),
                        e
                    ))
                }
            };

        match read_to_string(&proxy).await {
            Ok(data) => Ok(data),
            Err(e) => Err(format_err!(
                "could not read file `{}` as string: {}",
                path.as_path().display(),
                e
            )),
        }
    }

    // Read the contents of a file at the given `relative_path` as bytes.
    pub async fn read_file_bytes<P: AsRef<Path>>(&self, relative_path: P) -> Result<Vec<u8>> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("relative path is not valid unicode")),
        };

        let proxy =
            match open_file_no_describe(&self.proxy, relative_path, fio::OpenFlags::RIGHT_READABLE)
            {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!(
                        "could not open file `{}`: {}",
                        path.as_path().display(),
                        e
                    ))
                }
            };

        match read(&proxy).await {
            Ok(data) => Ok(data),
            Err(e) => Err(format_err!("could not read file `{}`: {}", path.as_path().display(), e)),
        }
    }

    // Checks if a file with the given `filename` exists in this directory.
    // Function is not recursive. Does not check subdirectories.
    pub async fn exists(&self, filename: &str) -> Result<bool> {
        match self.entry_names().await {
            Ok(entries) => Ok(entries.iter().any(|s| s == filename)),
            Err(e) => Err(format_err!(
                "could not check if `{}` exists in `{}`: {}",
                filename,
                self.path.as_path().display(),
                e
            )),
        }
    }

    // Finds entry with the given `filename` if it exists in this directory.
    // Function is not recursive. Does not check subdirectories.
    pub async fn entry_if_exists(&self, filename: &str) -> Result<Option<DirEntry>> {
        match self.entries().await {
            Ok(entries) => Ok(entries.into_iter().find(|e| e.name == filename)),
            Err(e) => Err(format_err!(
                "could not get entry names of `{}`: {}",
                self.path.as_path().display(),
                e
            )),
        }
    }

    // Remove the given `filename` from the directory. Note that while the file will be removed from
    // the directory, it will be destroyed only if there are no other references to it.
    pub async fn remove(&self, filename: &str) -> Result<()> {
        let options = fio::UnlinkOptions::EMPTY;
        match self.proxy.unlink(filename, options).await {
            Ok(r) => match r {
                Ok(()) => Ok(()),
                Err(e) => Err(format_err!(
                    "could not delete `{}` from `{}`: {}",
                    filename,
                    self.path.as_path().display(),
                    e
                )),
            },
            Err(e) => Err(format_err!(
                "proxy error while deleting `{}` from `{}`: {}",
                filename,
                self.path.as_path().display(),
                e
            )),
        }
    }

    // Attempts to create a new file at the given |relative_path|.
    // Replaces the file contents if it already exists.
    pub async fn create_file<P: AsRef<Path>>(&self, relative_path: P, data: &[u8]) -> Result<()> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("relative path is not valid unicode")),
        };

        let file = match open_file_no_describe(
            &self.proxy,
            relative_path,
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::CREATE,
        ) {
            Ok(proxy) => proxy,
            Err(e) => {
                return Err(format_err!(
                    "could not open file `{}`: {}",
                    path.as_path().display(),
                    e
                ))
            }
        };

        let () = file
            .resize(0)
            .await
            .map_err(|e| {
                format_err!("could not truncate file `{}`: {}", path.as_path().display(), e)
            })?
            .map_err(Status::from_raw)
            .map_err(|status| {
                format_err!("could not truncate file `{}`: {}", path.as_path().display(), status)
            })?;

        match write(&file, data).await {
            Ok(()) => {}
            Err(e) => {
                return Err(format_err!(
                    "could not write to file `{}`: {}",
                    path.as_path().display(),
                    e
                ))
            }
        }

        match close(file).await {
            Ok(()) => Ok(()),
            Err(e) => {
                Err(format_err!("could not close file `{}`: {}", path.as_path().display(), e))
            }
        }
    }

    // Returns the size of a file in bytes.
    pub async fn get_file_size<P: AsRef<Path>>(&self, relative_path: P) -> Result<u64> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("relative path is not valid unicode")),
        };

        let file =
            match open_file_no_describe(&self.proxy, relative_path, fio::OpenFlags::RIGHT_READABLE)
            {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!(
                        "could not open file `{}`: {}",
                        path.as_path().display(),
                        e
                    ))
                }
            };

        match file.get_attr().await {
            Ok((raw_status_code, attr)) => {
                Status::ok(raw_status_code)?;
                Ok(attr.storage_size)
            }
            Err(e) => {
                Err(format_err!("Unexpected FIDL error during file attribute retrieval: {}", e))
            }
        }
    }

    // Return a list of directory entry names in the directory.
    pub async fn entry_names(&self) -> Result<Vec<String>> {
        match self.entries().await {
            Ok(entries) => Ok(entries.into_iter().map(|e| e.name).collect()),
            Err(e) => Err(format_err!(
                "could not get entry names of `{}`: {}",
                self.path.as_path().display(),
                e
            )),
        }
    }

    // Return a clone of the existing proxy of the Directory.
    pub fn clone_proxy(&self) -> Result<fio::DirectoryProxy> {
        let (clone, clone_server) = create_endpoints::<fio::NodeMarker>()?;
        self.proxy.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, clone_server)?;

        match ClientEnd::<fio::DirectoryMarker>::new(clone.into_channel()).into_proxy() {
            Ok(cloned_proxy) => Ok(cloned_proxy),
            Err(e) => Err(format_err!("Could not clone proxy. {}", e)),
        }
    }

    pub fn clone(&self) -> Result<Self> {
        let proxy = clone_no_describe(&self.proxy, Some(fio::OpenFlags::RIGHT_READABLE))?;
        Ok(Self { path: self.path.clone(), proxy, readdir_mutex: Mutex::new(()) })
    }
}

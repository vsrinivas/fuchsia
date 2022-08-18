// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use std::{
    fs::{remove_file, File, OpenOptions},
    io::{ErrorKind, Write},
    path::{Path, PathBuf},
    time::{Duration, Instant},
};
use tracing::warn;

/// Opens and controls a lockfile against a specific filename using create-only
/// file modes, deleting the file on drop. Writes the current pid to the file
/// so it can be checked for if the process that created it is still around
/// (manually for now).
#[derive(Debug)]
pub struct Lockfile {
    path: PathBuf,
    handle: File,
}

#[derive(thiserror::Error, Debug)]
/// An error while creating a lockfile, including the underlying file io error
/// and if possible the error
#[error("Error obtaining lock on {:?} (existing owner if present: '{:?}', our pid is {pid})", .lock_path, .owner, pid=std::process::id())]
pub struct LockfileCreateError {
    /// The underlying error attempting to obtain the lockfile
    #[source]
    pub error: std::io::Error,
    /// The filename of the lockfile being attempted
    pub lock_path: PathBuf,
    /// Details about the lockfile's original owner, from the file.
    pub owner: Option<LockContext>,
}

impl LockfileCreateError {
    fn new(lock_path: &Path, error: std::io::Error) -> Self {
        // if it was an already-exists error and the file is there, try to read it and set
        let owner = match error.kind() {
            std::io::ErrorKind::AlreadyExists => {
                File::open(&lock_path).ok().and_then(|f| serde_json::from_reader(f).ok())
            }
            _ => None,
        };
        let lock_path = lock_path.to_owned();
        Self { error, lock_path, owner }
    }
}

/// A context identity used for the current process that can be used to identify where
/// a lockfile came from. A serialized version of this will be printed to the lockfile
/// when it's created.
///
/// It is not *guaranteed* to be unique across time, so it's not safe to assume that
/// `LockContext::current() == some_other_context` means it *was* the current process
/// or thread, but it should mean it wasn't if they're different.
#[derive(Serialize, Deserialize, Debug, Hash, PartialOrd, PartialEq)]
pub struct LockContext {
    pub pid: u32,
}

impl LockContext {
    pub fn current() -> Self {
        let pid = std::process::id();
        Self { pid }
    }

    pub fn write_to<W: Write>(&self, mut handle: W) -> Result<(), std::io::Error> {
        let context_str =
            serde_json::to_string(self).map_err(|e| std::io::Error::new(ErrorKind::Other, e))?;
        handle.write_all(context_str.as_bytes())
    }
}

impl Lockfile {
    /// Creates a lockfile at `filename` if possible. Returns the underlying error
    /// from the file create call. Note that this won't retry. Use [`Lockfile::lock`]
    /// or [`Lockfile::lock_for`] to do that.
    pub fn new(lock_path: &Path) -> Result<Self, LockfileCreateError> {
        // create the lock file -- if this succeeds the file exists now and is brand new
        // so we can write our details to it.
        OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(&lock_path)
            .and_then(|handle| {
                // but then once we've got the file, if we write to it and fail somehow
                // we have to clean up after ourselves, so immediately create the Lockfile
                // object, that way if we fail to write to it it will clean up after itself
                // in drop().
                let path = lock_path.to_owned();
                let mut lock_file = Self { handle, path };
                LockContext::current().write_to(&lock_file.handle)?;
                lock_file.handle.flush()?;
                Ok(lock_file)
            })
            .map_err(|e| LockfileCreateError::new(lock_path, e))
    }

    /// Creates a lockfile at `filename`.lock if possible. See [`Lockfile::new`] for details on the return value.
    pub fn new_for(filename: &Path) -> Result<Self, LockfileCreateError> {
        let mut lock_path = filename.to_owned();
        let filename = lock_path.file_name().map_or(".lock".to_string(), |name| {
            format!("{filename}.lock", filename = name.to_string_lossy())
        });

        lock_path.set_file_name(&filename);
        Self::new(&lock_path)
    }

    /// Creates a lockfile at `filename` if possible, retrying until it succeeds or times out.
    ///
    /// If unable to lock within the constraints, will return the error from the last attempt. It will
    /// try with an increasing sleep between attempts up to `timeout`.
    pub async fn lock(lock_path: &Path, timeout: Duration) -> Result<Self, LockfileCreateError> {
        let end_time = Instant::now() + timeout;
        let mut sleep_time = 10;
        loop {
            let lock_result = Self::new(lock_path);
            match lock_result {
                Ok(lockfile) => return Ok(lockfile),
                Err(e) if Instant::now() > end_time => return Err(e),
                _ => {
                    fuchsia_async::Timer::new(Duration::from_millis(sleep_time)).await;
                    // next time, retry with a longer wait, but not exponential, and not
                    // longer than it'll take to finish out the timeout.
                    let max_wait = end_time
                        .checked_duration_since(Instant::now())
                        .unwrap_or_default()
                        .as_millis()
                        .try_into()
                        .expect("Impossibly large wait time on lock");
                    sleep_time = (sleep_time * 2).clamp(0, max_wait);
                }
            };
        }
    }

    /// Creates a lockfile at `filename`.lock if possible, retrying until it succeeds or times out.
    ///
    /// See [`Lockfile::lock`] for more details.
    pub async fn lock_for(filename: &Path, timeout: Duration) -> Result<Self, LockfileCreateError> {
        let mut lock_path = filename.to_owned();
        let filename = lock_path.file_name().map_or(".lock".to_string(), |name| {
            format!("{filename}.lock", filename = name.to_string_lossy())
        });

        lock_path.set_file_name(&filename);
        Self::lock(&lock_path, timeout).await
    }

    /// Internal function used by both [`Self::unlock`] and [`Self::drop`]
    fn raw_unlock(lock_path: &Path) -> Result<(), std::io::Error> {
        // return an error unless it was just that the lockfile had already
        // been removed somehow (not a great sign but also not itself really
        // an error)
        match remove_file(lock_path) {
            Ok(()) => Ok(()),
            Err(e) if e.kind() == ErrorKind::NotFound => Ok(()),
            Err(e) => Err(e),
        }
    }

    /// Explicitly remove the lockfile and consume the lockfile object
    pub fn unlock(self) -> Result<(), std::io::Error> {
        Self::raw_unlock(&self.path)
    }

    pub fn path(&self) -> &Path {
        &self.path
    }
}

impl Drop for Lockfile {
    fn drop(&mut self) {
        if let Err(e) = Self::raw_unlock(&self.path) {
            // in Drop we can't really do much about this, so just warn about it
            warn!("Error removing lockfile {name}: {e:#?}", name = self.path.display());
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn create_lockfile_works() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let mut path = dir.path().to_owned();
        path.push("lockfile");
        let lock = Lockfile::new(&path)?;
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        assert!(
            Lockfile::new(&path).is_err(),
            "Should not be able to create lockfile at {path:?} while one already exists."
        );
        lock.unlock()?;
        assert!(!path.is_file(), "Lockfile {path:?} shouldn't exist");

        assert!(
            Lockfile::new(&path).is_ok(),
            "Should be able to make a new lockfile when the old one is unlocked."
        );

        Ok(())
    }
    #[test]
    fn create_lockfile_for_other_file_works() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let mut path = dir.path().to_owned();
        path.push("lockedfile");
        let lock = Lockfile::new_for(&path)?;
        path.set_file_name("lockedfile.lock");
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        assert!(
            Lockfile::new(&path).is_err(),
            "Should not be able to create lockfile at {path:?} while one already exists."
        );
        lock.unlock()?;
        assert!(!path.is_file(), "Lockfile {path:?} shouldn't exist");

        assert!(
            Lockfile::new(&path).is_ok(),
            "Should be able to make a new lockfile when the old one is unlocked."
        );

        Ok(())
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use nix::{
    errno::Errno,
    unistd::{self, Pid},
};
use serde::{Deserialize, Serialize};
use std::{
    fs::{remove_file, File, Metadata, OpenOptions},
    io::{ErrorKind, Write},
    path::{Path, PathBuf},
    time::{Duration, Instant, SystemTime},
};
use tracing::{error, info, warn};

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
    /// Metadata about the lockfile if it was available
    pub metadata: Option<Metadata>,
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
        let metadata = std::fs::metadata(lock_path).ok(); // purely advisory, ignore the error.
        let lock_path = lock_path.to_owned();
        Self { error, lock_path, owner, metadata }
    }

    /// Validates if a lockfile's info is inherently invalid and should be removed or ignored.
    pub fn is_valid(&self, at: SystemTime, my_pid: u32) -> bool {
        let ctime = self.metadata.as_ref().and_then(|metadata| metadata.created().ok());
        // just warnings/info
        match self.owner.as_ref() {
            Some(ctx) if ctx.pid == my_pid => {
                info!("Overlapping access to the lockfile at {path} from the same process, this may be a sign of other issues.", path=self.lock_path.display());
            }
            _ => (),
        };
        // actual errors
        match (self.owner.as_ref(), ctime) {
            (_, Some(ctime)) if ctime > at + Duration::from_secs(2) => {
                warn!("Lockfile {path} is in the future somehow, considering it invalid (ctime: {ctime:?}, now: {at:?})", path=self.lock_path.display());
                false
            }
            (None, Some(ctime)) if ctime + Duration::from_secs(10) < at => {
                warn!("Lockfile {path} is older than 10s, so is probably stale, considering it invalid (ctime {ctime:?}, now: {at:?})", path = self.lock_path.display());
                false
            }
            (Some(ctx), _)
                if unistd::getpgid(Some(Pid::from_raw(ctx.pid as i32)))
                    == Err(nix::Error::Sys(Errno::ESRCH)) =>
            {
                warn!("Lockfile {path} was created by a pid that no longer exists ({pid}), considering it invalid.", path=self.lock_path.display(), pid=ctx.pid);
                false
            }
            _ => true,
        }
    }

    /// Removes the lockfile if it exists, consuming the error if it was removed
    /// or returning the error back if it failed in any other way than already not-existing.
    pub fn remove_lock(self) -> Result<(), Self> {
        match remove_file(&self.lock_path) {
            Ok(_) => Ok(()),
            Err(e) if e.kind() == ErrorKind::NotFound => Ok(()),
            Err(e) => {
                error!(
                    "Error trying to remove lockfile {lockfile}: {e:?}",
                    lockfile = self.lock_path.display()
                );
                Err(self)
            }
        }
    }
}

/// A context identity used for the current process that can be used to identify where
/// a lockfile came from. A serialized version of this will be printed to the lockfile
/// when it's created.
///
/// It is not *guaranteed* to be unique across time, so it's not safe to assume that
/// `LockContext::current() == some_other_context` means it *was* the current process
/// or thread, but it should mean it wasn't if they're different.
#[derive(Clone, Serialize, Deserialize, Debug, Hash, PartialOrd, PartialEq)]
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
    pub fn new(lock_path: &Path, context: LockContext) -> Result<Self, LockfileCreateError> {
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
                context.write_to(&lock_file.handle)?;
                lock_file.handle.flush()?;
                Ok(lock_file)
            })
            .map_err(|e| LockfileCreateError::new(lock_path, e))
    }

    /// Creates a lockfile at `filename`.lock if possible. See [`Lockfile::new`] for details on the return value.
    pub fn new_for(filename: &Path, context: LockContext) -> Result<Self, LockfileCreateError> {
        let mut lock_path = filename.to_owned();
        let filename = lock_path.file_name().map_or(".lock".to_string(), |name| {
            format!("{filename}.lock", filename = name.to_string_lossy())
        });

        lock_path.set_file_name(&filename);
        Self::new(&lock_path, context)
    }

    /// Creates a lockfile at `filename` if possible, retrying until it succeeds or times out.
    ///
    /// If unable to lock within the constraints, will return the error from the last attempt. It will
    /// try with an increasing sleep between attempts up to `timeout`.
    async fn lock(lock_path: &Path, timeout: Duration) -> Result<Self, LockfileCreateError> {
        let end_time = Instant::now() + timeout;
        let context = LockContext::current();
        let mut sleep_time = 10;
        loop {
            let lock_result = Self::new(lock_path, context.clone());
            match lock_result {
                Ok(lockfile) => return Ok(lockfile),
                Err(e) if !e.is_valid(SystemTime::now(), context.pid) => {
                    // try to remove the invalid lockfile and immediately continue. If we get
                    // any error other than ENOTFOUND, there's likely an underlying issue
                    // with the filesystem and we should just bail immediately.
                    info!("Removing invalid lockfile {lockfile}", lockfile = lock_path.display());
                    e.remove_lock()?;
                }
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
        let path = dir.path().join("lockedfile");
        let lock = Lockfile::new(&path, LockContext::current())?;
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        assert!(
            Lockfile::new(&path, LockContext::current()).is_err(),
            "Should not be able to create lockfile at {path:?} while one already exists."
        );
        lock.unlock()?;
        assert!(!path.is_file(), "Lockfile {path:?} shouldn't exist");

        assert!(
            Lockfile::new(&path, LockContext::current()).is_ok(),
            "Should be able to make a new lockfile when the old one is unlocked."
        );

        Ok(())
    }

    #[test]
    fn create_lockfile_for_other_file_works() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let mut path = dir.path().join("lockedfile");
        let lock = Lockfile::new_for(&path, LockContext::current())?;
        path.set_file_name("lockedfile.lock");
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        assert!(
            Lockfile::new(&path, LockContext::current()).is_err(),
            "Should not be able to create lockfile at {path:?} while one already exists."
        );
        lock.unlock()?;
        assert!(!path.is_file(), "Lockfile {path:?} shouldn't exist");

        assert!(
            Lockfile::new(&path, LockContext::current()).is_ok(),
            "Should be able to make a new lockfile when the old one is unlocked."
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn lock_with_timeout() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let mut path = dir.path().join("lockedfile");
        let lock = Lockfile::lock_for(&path, Duration::from_secs(1)).await?;
        path.set_file_name("lockedfile.lock");
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        Lockfile::lock(&path, Duration::from_secs(1))
            .await
            .err()
            .expect("Shouldn't be able to re-lock lock file with timeout");

        lock.unlock()?;
        assert!(!path.is_file(), "Lockfile {path:?} shouldn't exist");

        Lockfile::lock(&path, Duration::from_secs(1)).await?;

        Ok(())
    }

    #[test]
    fn lock_file_validity() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let path = dir.path().join("lockedfile.lock");
        let _lock = Lockfile::new(&path, LockContext::current())?;
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        let err = Lockfile::new(&path, LockContext::current())
            .err()
            .expect("Should not be able to re-create lockfile");

        let now = SystemTime::now();
        let real_pid = std::process::id();
        assert!(
            err.is_valid(now, real_pid),
            "A just created lockfile should be valid from the same process (but may warn)"
        );
        assert!(
            err.is_valid(now, real_pid + 1),
            "A just created lockfile should be valid from another process"
        );
        assert!(
            !err.is_valid(now - Duration::from_secs(9999), real_pid),
            "A lockfile from the far future should be valid"
        );

        Ok(())
    }

    #[test]
    fn ownerless_lockfile_validity() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let path = dir.path().join("lockedfile.lock");
        let _lock = File::create(&path).expect("Creating empty lock file");
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        let err = Lockfile::new(&path, LockContext::current())
            .err()
            .expect("Should not be able to re-create lockfile");

        let now = SystemTime::now();
        let real_pid = std::process::id();
        assert!(
            err.is_valid(now, real_pid),
            "An ownerless lockfile that's fresh should be considered valid"
        );

        assert!(
            !err.is_valid(now + Duration::from_secs(9999), real_pid),
            "An ownerless lockfile from a long time ago should be invalid"
        );

        Ok(())
    }

    #[test]
    fn non_running_pid_lockfile_validity() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let path = dir.path().join("lockedfile.lock");
        let _lock = Lockfile::new(&path, LockContext { pid: u32::MAX })?;
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        let err = Lockfile::new(&path, LockContext::current())
            .err()
            .expect("Should not be able to re-create lockfile");

        let now = SystemTime::now();
        let real_pid = std::process::id();
        assert!(
            !err.is_valid(now, real_pid),
            "A lockfile owned by a pid that isn't running should be invalid"
        );

        Ok(())
    }

    #[test]
    fn force_delete_nonexistent_lockfile_ok() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let path = dir.path().join("lockedfile.lock");
        let bogus_error = LockfileCreateError::new(
            &path,
            std::io::Error::new(std::io::ErrorKind::Other, "stuff"),
        );
        bogus_error.remove_lock().expect("Removing non-existent lock file");
        Ok(())
    }

    #[test]
    fn force_delete_real_lockfile_ok() -> Result<(), anyhow::Error> {
        let dir = tempfile::TempDir::new()?;
        let mut path = dir.path().join("lockedfile");
        let lock = Lockfile::new_for(&path, LockContext::current())?;
        path.set_file_name("lockedfile.lock");
        assert!(path.is_file(), "Lockfile {path:?} should exist");

        let err = Lockfile::new(&path, LockContext::current())
            .err()
            .expect("Should not be able to re-create lockfile");
        err.remove_lock().expect("Should be able to remove lock file from error");

        assert!(!path.is_file(), "Lockfile {path:?} should have been deleted");

        lock.unlock().expect("Unlock should have succeeded even though lock file was already gone");

        Ok(())
    }
}

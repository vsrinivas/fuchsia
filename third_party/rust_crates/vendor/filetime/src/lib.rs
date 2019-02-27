//! Timestamps for files in Rust
//!
//! This library provides platform-agnostic inspection of the various timestamps
//! present in the standard `fs::Metadata` structure.
//!
//! # Installation
//!
//! Add this to your `Cargo.toml`:
//!
//! ```toml
//! [dependencies]
//! filetime = "0.1"
//! ```
//!
//! # Usage
//!
//! ```no_run
//! use std::fs;
//! use filetime::FileTime;
//!
//! let metadata = fs::metadata("foo.txt").unwrap();
//!
//! let mtime = FileTime::from_last_modification_time(&metadata);
//! println!("{}", mtime);
//!
//! let atime = FileTime::from_last_access_time(&metadata);
//! assert!(mtime < atime);
//!
//! // Inspect values that can be interpreted across platforms
//! println!("{}", mtime.unix_seconds());
//! println!("{}", mtime.nanoseconds());
//!
//! // Print the platform-specific value of seconds
//! println!("{}", mtime.seconds());
//! ```

#[macro_use]
extern crate cfg_if;

use std::fmt;
use std::fs;
use std::io;
use std::path::Path;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

cfg_if! {
    if #[cfg(target_os = "redox")] {
        #[path = "redox.rs"]
        mod imp;
    } else if #[cfg(windows)] {
        #[path = "windows.rs"]
        mod imp;
    } else {
        #[path = "unix/mod.rs"]
        mod imp;
    }
}

/// A helper structure to represent a timestamp for a file.
///
/// The actual value contined within is platform-specific and does not have the
/// same meaning across platforms, but comparisons and stringification can be
/// significant among the same platform.
#[derive(Eq, PartialEq, Ord, PartialOrd, Debug, Copy, Clone, Hash)]
pub struct FileTime {
    seconds: i64,
    nanos: u32,
}

impl FileTime {
    /// Creates a new timestamp representing a 0 time.
    ///
    /// Useful for creating the base of a cmp::max chain of times.
    pub fn zero() -> FileTime {
        FileTime { seconds: 0, nanos: 0 }
    }

    /// Creates a new instance of `FileTime` with a number of seconds and
    /// nanoseconds relative to the Unix epoch, 1970-01-01T00:00:00Z.
    ///
    /// Negative seconds represent times before the Unix epoch, and positive
    /// values represent times after it. Nanos always count forwards in time.
    ///
    /// Note that this is typically the relative point that Unix time stamps are
    /// from, but on Windows the native time stamp is relative to January 1,
    /// 1601 so the return value of `seconds` from the returned `FileTime`
    /// instance may not be the same as that passed in.
    pub fn from_unix_time(seconds: i64, nanos: u32) -> FileTime {
        FileTime {
            seconds: seconds + if cfg!(windows) {11644473600} else {0},
            nanos,
        }
    }

    /// Creates a new timestamp from the last modification time listed in the
    /// specified metadata.
    ///
    /// The returned value corresponds to the `mtime` field of `stat` on Unix
    /// platforms and the `ftLastWriteTime` field on Windows platforms.
    pub fn from_last_modification_time(meta: &fs::Metadata) -> FileTime {
        imp::from_last_modification_time(meta)
    }

    /// Creates a new timestamp from the last access time listed in the
    /// specified metadata.
    ///
    /// The returned value corresponds to the `atime` field of `stat` on Unix
    /// platforms and the `ftLastAccessTime` field on Windows platforms.
    pub fn from_last_access_time(meta: &fs::Metadata) -> FileTime {
        imp::from_last_access_time(meta)
    }

    /// Creates a new timestamp from the creation time listed in the specified
    /// metadata.
    ///
    /// The returned value corresponds to the `birthtime` field of `stat` on
    /// Unix platforms and the `ftCreationTime` field on Windows platforms. Note
    /// that not all Unix platforms have this field available and may return
    /// `None` in some circumstances.
    pub fn from_creation_time(meta: &fs::Metadata) -> Option<FileTime> {
        imp::from_creation_time(meta)
    }

    /// Creates a new timestamp from the given SystemTime.
    ///
    /// Windows counts file times since 1601-01-01T00:00:00Z, and cannot
    /// represent times before this, but it's possible to create a SystemTime
    /// that does. This function will error if passed such a SystemTime.
    pub fn from_system_time(time: SystemTime) -> FileTime {
        let epoch = if cfg!(windows) {
            UNIX_EPOCH - Duration::from_secs(11644473600)
        } else {
            UNIX_EPOCH
        };

        time.duration_since(epoch).map(|d| FileTime {
            seconds: d.as_secs() as i64,
            nanos: d.subsec_nanos()
        })
        .unwrap_or_else(|e| {
            let until_epoch = e.duration();
            let (sec_offset, nanos) = if until_epoch.subsec_nanos() == 0 {
                (0, 0)
            } else {
                (-1, 1_000_000_000 - until_epoch.subsec_nanos())
            };

            FileTime {
                seconds: -1 * until_epoch.as_secs() as i64 + sec_offset,
                nanos
            }
        })
    }

    /// Returns the whole number of seconds represented by this timestamp.
    ///
    /// Note that this value's meaning is **platform specific**. On Unix
    /// platform time stamps are typically relative to January 1, 1970, but on
    /// Windows platforms time stamps are relative to January 1, 1601.
    pub fn seconds(&self) -> i64 { self.seconds }

    /// Returns the whole number of seconds represented by this timestamp,
    /// relative to the Unix epoch start of January 1, 1970.
    ///
    /// Note that this does not return the same value as `seconds` for Windows
    /// platforms as seconds are relative to a different date there.
    pub fn unix_seconds(&self) -> i64 {
        self.seconds - if cfg!(windows) {11644473600} else {0}
    }

    /// Returns the nanosecond precision of this timestamp.
    ///
    /// The returned value is always less than one billion and represents a
    /// portion of a second forward from the seconds returned by the `seconds`
    /// method.
    pub fn nanoseconds(&self) -> u32 { self.nanos }
}

impl fmt::Display for FileTime {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}.{:09}s", self.seconds, self.nanos)
    }
}

impl From<SystemTime> for FileTime {
    fn from(time: SystemTime) -> FileTime {
        FileTime::from_system_time(time)
    }
}

/// Set the last access and modification times for a file on the filesystem.
///
/// This function will set the `atime` and `mtime` metadata fields for a file
/// on the local filesystem, returning any error encountered.
pub fn set_file_times<P>(p: P, atime: FileTime, mtime: FileTime)
    -> io::Result<()>
    where P: AsRef<Path>
{
    imp::set_file_times(p.as_ref(), atime, mtime)
}

/// Set the last access and modification times for a file on the filesystem.
/// This function does not follow symlink.
///
/// This function will set the `atime` and `mtime` metadata fields for a file
/// on the local filesystem, returning any error encountered.
pub fn set_symlink_file_times<P>(p: P, atime: FileTime, mtime: FileTime)
    -> io::Result<()>
    where P: AsRef<Path>
{
    imp::set_symlink_file_times(p.as_ref(), atime, mtime)
}

#[cfg(test)]
mod tests {
    extern crate tempdir;

    use std::io;
    use std::path::Path;
    use std::fs::{self, File};
    use self::tempdir::TempDir;
    use super::{FileTime, set_file_times, set_symlink_file_times};
    use std::time::{Duration, UNIX_EPOCH};

    #[cfg(unix)]
    fn make_symlink<P,Q>(src: P, dst: Q) -> io::Result<()>
        where P: AsRef<Path>,
              Q: AsRef<Path>,
    {
        use std::os::unix::fs::symlink;
        symlink(src, dst)
    }

    #[cfg(windows)]
    fn make_symlink<P,Q>(src: P, dst: Q) -> io::Result<()>
        where P: AsRef<Path>,
              Q: AsRef<Path>,
    {
        use std::os::windows::fs::symlink_file;
        symlink_file(src, dst)
    }

    #[test]
    #[cfg(windows)]
    fn from_unix_time_test() {
        let time = FileTime::from_unix_time(10, 100_000_000);
        assert_eq!(11644473610, time.seconds);
        assert_eq!(100_000_000, time.nanos);

        let time = FileTime::from_unix_time(-10, 100_000_000);
        assert_eq!(11644473590, time.seconds);
        assert_eq!(100_000_000, time.nanos);

        let time = FileTime::from_unix_time(-12_000_000_000, 0);
        assert_eq!(-355526400, time.seconds);
        assert_eq!(0, time.nanos);
    }

    #[test]
    #[cfg(not(windows))]
    fn from_unix_time_test() {
        let time = FileTime::from_unix_time(10, 100_000_000);
        assert_eq!(10, time.seconds);
        assert_eq!(100_000_000, time.nanos);

        let time = FileTime::from_unix_time(-10, 100_000_000);
        assert_eq!(-10, time.seconds);
        assert_eq!(100_000_000, time.nanos);

        let time = FileTime::from_unix_time(-12_000_000_000, 0);
        assert_eq!(-12_000_000_000, time.seconds);
        assert_eq!(0, time.nanos);
    }

    #[test]
    #[cfg(windows)]
    fn from_system_time_test() {
        let time = FileTime::from_system_time(UNIX_EPOCH + Duration::from_secs(10));
        assert_eq!(11644473610, time.seconds);
        assert_eq!(0, time.nanos);

        let time = FileTime::from_system_time(UNIX_EPOCH - Duration::from_secs(10));
        assert_eq!(11644473590, time.seconds);
        assert_eq!(0, time.nanos);

        let time = FileTime::from_system_time(UNIX_EPOCH - Duration::from_millis(1100));
        assert_eq!(11644473598, time.seconds);
        assert_eq!(900_000_000, time.nanos);

        let time = FileTime::from_system_time(UNIX_EPOCH - Duration::from_secs(12_000_000_000));
        assert_eq!(-355526400, time.seconds);
        assert_eq!(0, time.nanos);
    }

    #[test]
    #[cfg(not(windows))]
    fn from_system_time_test() {
        let time = FileTime::from_system_time(UNIX_EPOCH + Duration::from_secs(10));
        assert_eq!(10, time.seconds);
        assert_eq!(0, time.nanos);

        let time = FileTime::from_system_time(UNIX_EPOCH - Duration::from_secs(10));
        assert_eq!(-10, time.seconds);
        assert_eq!(0, time.nanos);

        let time = FileTime::from_system_time(UNIX_EPOCH - Duration::from_millis(1100));
        assert_eq!(-2, time.seconds);
        assert_eq!(900_000_000, time.nanos);

        let time = FileTime::from_system_time(UNIX_EPOCH - Duration::from_secs(12_000_000));
        assert_eq!(-12_000_000, time.seconds);
        assert_eq!(0, time.nanos);
    }

    #[test]
    fn set_file_times_test() {
        let td = TempDir::new("filetime").unwrap();
        let path = td.path().join("foo.txt");
        File::create(&path).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        let atime = FileTime::from_last_access_time(&metadata);
        set_file_times(&path, atime, mtime).unwrap();

        let new_mtime = FileTime::from_unix_time(10_000, 0);
        set_file_times(&path, atime, new_mtime).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_mtime);

        let spath = td.path().join("bar.txt");
        make_symlink(&path, &spath).unwrap();
        let metadata = fs::symlink_metadata(&spath).unwrap();
        let smtime = FileTime::from_last_modification_time(&metadata);

        set_file_times(&spath, atime, mtime).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let cur_mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, cur_mtime);

        let metadata = fs::symlink_metadata(&spath).unwrap();
        let cur_mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(smtime, cur_mtime);

        set_file_times(&spath, atime, new_mtime).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_mtime);

        let metadata = fs::symlink_metadata(&spath).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, smtime);
    }

    #[test]
    fn set_file_times_pre_unix_epoch_test() {
        let td = TempDir::new("filetime").unwrap();
        let path = td.path().join("foo.txt");
        File::create(&path).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        let atime = FileTime::from_last_access_time(&metadata);
        set_file_times(&path, atime, mtime).unwrap();

        let new_mtime = FileTime::from_unix_time(-10_000, 0);
        set_file_times(&path, atime, new_mtime).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_mtime);
    }

    #[test]
    #[cfg(windows)]
    fn set_file_times_pre_windows_epoch_test() {
        let td = TempDir::new("filetime").unwrap();
        let path = td.path().join("foo.txt");
        File::create(&path).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        let atime = FileTime::from_last_access_time(&metadata);
        set_file_times(&path, atime, mtime).unwrap();

        let new_mtime = FileTime::from_unix_time(-12_000_000_000, 0);
        assert!(set_file_times(&path, atime, new_mtime).is_err());
    }

    #[test]
    fn set_symlink_file_times_test() {
        let td = TempDir::new("filetime").unwrap();
        let path = td.path().join("foo.txt");
        File::create(&path).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        let atime = FileTime::from_last_access_time(&metadata);
        set_symlink_file_times(&path, atime, mtime).unwrap();

        let new_mtime = FileTime::from_unix_time(10_000, 0);
        set_symlink_file_times(&path, atime, new_mtime).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_mtime);

        let spath = td.path().join("bar.txt");
        make_symlink(&path, &spath).unwrap();

        let metadata = fs::symlink_metadata(&spath).unwrap();
        let smtime = FileTime::from_last_modification_time(&metadata);
        let satime = FileTime::from_last_access_time(&metadata);
        set_symlink_file_times(&spath, smtime, satime).unwrap();

        let metadata = fs::metadata(&path).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_mtime);

        let new_smtime = FileTime::from_unix_time(20_000, 0);
        set_symlink_file_times(&spath, atime, new_smtime).unwrap();

        let metadata = fs::metadata(&spath).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_mtime);

        let metadata = fs::symlink_metadata(&spath).unwrap();
        let mtime = FileTime::from_last_modification_time(&metadata);
        assert_eq!(mtime, new_smtime);
    }
}

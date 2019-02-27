use std::path::Path;
use std::io;

use FileTime;
use super::libc;

pub fn set_file_times(p: &Path, atime: FileTime, mtime: FileTime) -> io::Result<()> {
    super::utimes(p, atime, mtime, libc::utimes)
}

pub fn set_symlink_file_times(p: &Path, atime: FileTime, mtime: FileTime) -> io::Result<()> {
    super::utimes(p, atime, mtime, libc::lutimes)
}

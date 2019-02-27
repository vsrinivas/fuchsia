use std::path::Path;
use std::io;

use FileTime;
use super::libc;

pub fn set_file_times(p: &Path, atime: FileTime, mtime: FileTime) -> io::Result<()> {
    super::utimensat(p, atime, mtime, libc::utimensat, 0)
}

pub fn set_symlink_file_times(p: &Path, atime: FileTime, mtime: FileTime) -> io::Result<()> {
    super::utimensat(p, atime, mtime, libc::utimensat, libc::AT_SYMLINK_NOFOLLOW)
}

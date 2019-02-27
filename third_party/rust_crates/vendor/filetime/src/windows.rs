#![allow(bad_style)]

use std::fs::{self, OpenOptions};
use std::io;
use std::os::windows::prelude::*;
use std::path::Path;

use FileTime;

pub fn set_file_times(p: &Path, atime: FileTime, mtime: FileTime) -> io::Result<()> {
    set_file_times_w(p, atime, mtime, OpenOptions::new())
}

pub fn set_symlink_file_times(p: &Path, atime: FileTime, mtime: FileTime) -> io::Result<()> {
    use std::os::windows::fs::OpenOptionsExt;
    const FILE_FLAG_OPEN_REPARSE_POINT: u32 = 0x00200000;

    let mut options = OpenOptions::new();
    options.custom_flags(FILE_FLAG_OPEN_REPARSE_POINT);
    set_file_times_w(p, atime, mtime, options)
}

pub fn set_file_times_w(p: &Path,
                        atime: FileTime,
                        mtime: FileTime,
                        mut options: OpenOptions) -> io::Result<()> {
    type BOOL = i32;
    type HANDLE = *mut u8;
    type DWORD = u32;

    #[repr(C)]
    struct FILETIME {
        dwLowDateTime: u32,
        dwHighDateTime: u32,
    }

    extern "system" {
        fn SetFileTime(hFile: HANDLE,
                       lpCreationTime: *const FILETIME,
                       lpLastAccessTime: *const FILETIME,
                       lpLastWriteTime: *const FILETIME) -> BOOL;
    }

    let f = try!(options.write(true).open(p));
    let atime = to_filetime(&atime);
    let mtime = to_filetime(&mtime);
    return unsafe {
        let ret = SetFileTime(f.as_raw_handle() as *mut _,
                              0 as *const _,
                              &atime, &mtime);
        if ret != 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        }
    };

    fn to_filetime(ft: &FileTime) -> FILETIME {
        let intervals = ft.seconds() * (1_000_000_000 / 100) +
                        ((ft.nanoseconds() as i64) / 100);
        FILETIME {
            dwLowDateTime: intervals as DWORD,
            dwHighDateTime: (intervals >> 32) as DWORD,
        }
    }
}

pub fn from_last_modification_time(meta: &fs::Metadata) -> FileTime {
    from_intervals(meta.last_write_time())
}

pub fn from_last_access_time(meta: &fs::Metadata) -> FileTime {
    from_intervals(meta.last_access_time())
}

pub fn from_creation_time(meta: &fs::Metadata) -> Option<FileTime> {
    Some(from_intervals(meta.creation_time()))
}

fn from_intervals(ticks: u64) -> FileTime {
    // Windows write times are in 100ns intervals, so do a little math to
    // get it into the right representation.
    FileTime {
        seconds: (ticks / (1_000_000_000 / 100)) as i64,
        nanos: ((ticks % (1_000_000_000 / 100)) * 100) as u32,
    }
}

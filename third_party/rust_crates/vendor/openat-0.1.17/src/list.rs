use std::io;
use std::ptr;
use std::ffi::{CStr, OsStr};
use std::os::unix::ffi::OsStrExt;

use libc;

use {Dir, Entry, SimpleType};


// We have such weird constants because C types are ugly
const DOT: [libc::c_char; 2] = [b'.' as libc::c_char, 0];
const DOTDOT: [libc::c_char; 3] = [b'.' as libc::c_char, b'.' as libc::c_char, 0];


/// Iterator over directory entries
///
/// Created using `Dir::list_dir()`
#[derive(Debug)]
pub struct DirIter {
    dir: *mut libc::DIR,
}

impl Entry {
    /// Returns the file name of this entry
    pub fn file_name(&self) -> &OsStr {
        OsStr::from_bytes(self.name.to_bytes())
    }
    /// Returns the simplified type of this entry
    pub fn simple_type(&self) -> Option<SimpleType> {
        self.file_type
    }
}

#[cfg(any(target_os="linux", target_os="fuchsia"))]
unsafe fn errno_location() -> *mut libc::c_int {
    libc::__errno_location()
}

#[cfg(any(target_os="openbsd", target_os="netbsd", target_os="android"))]
unsafe fn errno_location() -> *mut libc::c_int {
    libc::__errno()
}

#[cfg(not(any(target_os="linux", target_os="openbsd", target_os="netbsd", target_os="android", target_os="fuchsia")))]
unsafe fn errno_location() -> *mut libc::c_int {
    libc::__error()
}

impl DirIter {

    unsafe fn next_entry(&mut self) -> io::Result<Option<*const libc::dirent>>
    {
        // Reset errno to detect if error occurred
        *errno_location() = 0;

        let entry = libc::readdir(self.dir);
        if entry == ptr::null_mut() {
            if *errno_location() == 0 {
                return Ok(None)
            } else {
                return Err(io::Error::last_os_error());
            }
        }
        return Ok(Some(entry));
    }
}

pub fn open_dir(dir: &Dir, path: &CStr) -> io::Result<DirIter> {
    let dir_fd = unsafe {
        libc::openat(dir.0, path.as_ptr(), libc::O_DIRECTORY|libc::O_CLOEXEC)
    };
    if dir_fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        let dir = unsafe { libc::fdopendir(dir_fd) };
        if dir == ptr::null_mut() {
            Err(io::Error::last_os_error())
        } else {
            Ok(DirIter { dir: dir })
        }
    }
}

impl Iterator for DirIter {
    type Item = io::Result<Entry>;
    fn next(&mut self) -> Option<Self::Item> {
        unsafe {
            loop {
                match self.next_entry() {
                    Err(e) => return Some(Err(e)),
                    Ok(None) => return None,
                    Ok(Some(e)) if (*e).d_name[..2] == DOT => continue,
                    Ok(Some(e)) if (*e).d_name[..3] == DOTDOT => continue,
                    Ok(Some(e)) => {
                        return Some(Ok(Entry {
                            name: CStr::from_ptr((&(*e).d_name).as_ptr())
                                .to_owned(),
                            file_type: match (*e).d_type {
                                0 => None,
                                libc::DT_REG => Some(SimpleType::File),
                                libc::DT_DIR => Some(SimpleType::Dir),
                                libc::DT_LNK => Some(SimpleType::Symlink),
                                _ => Some(SimpleType::Other),
                            },
                        }));
                    }
                }
            }
        }
    }
}

impl Drop for DirIter {
    fn drop(&mut self) {
        unsafe {
            libc::closedir(self.dir);
        }
    }
}

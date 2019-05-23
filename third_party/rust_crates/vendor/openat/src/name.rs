use std::ffi::{OsStr, CStr, CString};
use std::path::{Path, PathBuf};
use std::os::unix::ffi::OsStrExt;

use {Entry};


/// The purpose of this is similar to `AsRef<Path>` but it's optimized for
/// things that can be directly used as `CStr` (which is type passed to
/// the underlying system call).
///
/// This trait should be implemented for everything for which `AsRef<Path>`
/// is implemented
pub trait AsPath {
    /// The return value of the `to_path` that holds data copied from the
    /// original path (if copy is needed, otherwise it's just a reference)
    type Buffer: AsRef<CStr>;
    /// Returns `None` when path contains a zero byte
    fn to_path(self) -> Option<Self::Buffer>;
}

impl<'a> AsPath for &'a Path {
    type Buffer = CString;
    fn to_path(self) -> Option<CString> {
        CString::new(self.as_os_str().as_bytes()).ok()
    }
}

impl<'a> AsPath for &'a PathBuf {
    type Buffer = CString;
    fn to_path(self) -> Option<CString> {
        CString::new(self.as_os_str().as_bytes()).ok()
    }
}

impl<'a> AsPath for &'a OsStr {
    type Buffer = CString;
    fn to_path(self) -> Option<CString> {
        CString::new(self.as_bytes()).ok()
    }
}

impl<'a> AsPath for &'a str {
    type Buffer = CString;
    fn to_path(self) -> Option<CString> {
        CString::new(self.as_bytes()).ok()
    }
}

impl<'a> AsPath for &'a String {
    type Buffer = CString;
    fn to_path(self) -> Option<CString> {
        CString::new(self.as_bytes()).ok()
    }
}

impl<'a> AsPath for String {
    type Buffer = CString;
    fn to_path(self) -> Option<CString> {
        CString::new(self).ok()
    }
}

impl<'a> AsPath for &'a CStr {
    type Buffer = &'a CStr;
    fn to_path(self) -> Option<&'a CStr> {
        Some(self)
    }
}

impl<'a> AsPath for &'a Entry {
    type Buffer = &'a CStr;
    fn to_path(self) -> Option<&'a CStr> {
        Some(&self.name)
    }
}

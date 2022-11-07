use std::fs::Permissions;
use std::os::unix::fs::PermissionsExt;

use libc;

use SimpleType;


/// A file metadata
///
/// Because we can't freely create a `std::fs::Metadata` object we have to
/// implement our own structure.
pub struct Metadata {
    stat: libc::stat,
}

impl Metadata {
    /// Returns simplified type of the directory entry
    pub fn simple_type(&self) -> SimpleType {
        let typ = self.stat.st_mode & libc::S_IFMT;
        match typ {
            libc::S_IFREG => SimpleType::File,
            libc::S_IFDIR => SimpleType::Dir,
            libc::S_IFLNK => SimpleType::Symlink,
            _ => SimpleType::Other,
        }
    }
    /// Returns underlying stat structure
    pub fn stat(&self) -> &libc::stat {
        &self.stat
    }
    /// Returns `true` if the entry is a regular file
    pub fn is_file(&self) -> bool {
        self.simple_type() == SimpleType::File
    }
    /// Returns `true` if the entry is a directory
    pub fn is_dir(&self) -> bool {
        self.simple_type() == SimpleType::Dir
    }
    /// Returns permissions of the entry
    pub fn permissions(&self) -> Permissions {
        Permissions::from_mode(self.stat.st_mode as u32)
    }
    /// Returns file size
    pub fn len(&self) -> u64 {
        self.stat.st_size as u64
    }
}

pub fn new(stat: libc::stat) -> Metadata {
    Metadata { stat: stat }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn dir() {
        let d = ::Dir::open(".").unwrap();
        let m = d.metadata("src").unwrap();
        assert_eq!(m.simple_type(), SimpleType::Dir);
        assert!(m.is_dir());
        assert!(!m.is_file());
    }

    #[test]
    fn file() {
        let d = ::Dir::open("src").unwrap();
        let m = d.metadata("lib.rs").unwrap();
        assert_eq!(m.simple_type(), SimpleType::File);
        assert!(!m.is_dir());
        assert!(m.is_file());
    }
}

use std::fs::Metadata;

/// This is a simplified file type enum that is easy to match
///
/// It doesn't represent all the options, because that enum needs to extensible
/// but most application do not actually need that power, so we provide
/// this simplified enum that works for many appalications.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SimpleType {
    /// Entry is a symlink
    Symlink,
    /// Entry is a directory
    Dir,
    /// Entry is a regular file
    File,
    /// Entry is neither a symlink, directory nor a regular file
    Other,
}

impl SimpleType {
    /// Find out a simple type from a file Metadata (stat)
    pub fn extract(stat: &Metadata) -> SimpleType {
        if stat.file_type().is_symlink() {
            SimpleType::Symlink
        } else if stat.is_dir() {
            SimpleType::Dir
        } else if stat.is_file() {
            SimpleType::File
        } else {
            SimpleType::Other
        }
    }
}

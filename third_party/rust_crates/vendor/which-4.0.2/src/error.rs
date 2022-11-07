use thiserror;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(thiserror::Error, Copy, Clone, Eq, PartialEq, Debug)]
pub enum Error {
    #[error("bad absolute path")]
    BadAbsolutePath,
    #[error("bad relative path")]
    BadRelativePath,
    #[error("cannot find binary path")]
    CannotFindBinaryPath,
    #[error("cannot get current directory")]
    CannotGetCurrentDir,
    #[error("cannot canonicalize path")]
    CannotCanonicalize,
}

//! Disk-related types and helper functions.

use super::{GptConfig, GptDisk};
use std::{convert::TryFrom, fmt, io, path};

/// Default size of a logical sector (bytes).
pub const DEFAULT_SECTOR_SIZE: LogicalBlockSize = LogicalBlockSize::Lb512;

/// Logical block/sector size of a GPT disk.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LogicalBlockSize {
    /// 512 bytes.
    Lb512,
    /// 4096 bytes.
    Lb4096,
}

impl From<LogicalBlockSize> for u64 {
    fn from(lb: LogicalBlockSize) -> u64 {
        match lb {
            LogicalBlockSize::Lb512 => 512,
            LogicalBlockSize::Lb4096 => 4096,
        }
    }
}

impl From<LogicalBlockSize> for usize {
    fn from(lb: LogicalBlockSize) -> usize {
        match lb {
            LogicalBlockSize::Lb512 => 512,
            LogicalBlockSize::Lb4096 => 4096,
        }
    }
}

impl TryFrom<u64> for LogicalBlockSize {
    type Error = io::Error;
    fn try_from(v: u64) -> Result<Self, Self::Error> {
        match v {
            512 => Ok(LogicalBlockSize::Lb512),
            4096 => Ok(LogicalBlockSize::Lb4096),
            _ => Err(io::Error::new(
                io::ErrorKind::Other,
                "unsupported logical block size (must be 512 or 4096)"
            )),
        }
    }
}

impl fmt::Display for LogicalBlockSize {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            LogicalBlockSize::Lb512 => write!(f, "512"),
            LogicalBlockSize::Lb4096 => write!(f, "4096"),
        }
    }
}

/// Open and read a GPT disk, using default configuration options.
///
/// ## Example
///
/// ```rust,no_run
/// let gpt_disk = gpt::disk::read_disk("/dev/sdz").unwrap();
/// println!("{:#?}", gpt_disk);
/// ```
pub fn read_disk(diskpath: impl AsRef<path::Path>) -> io::Result<GptDisk<'static>> {
    let cfg = GptConfig::new();
    cfg.open(diskpath)
}

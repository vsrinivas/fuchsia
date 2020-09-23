#[cfg(feature = "std")]
use std::error::Error;

use crate::{core::fmt, io};

#[derive(fmt::Debug)]
pub enum FatfsNumericError {
    NotPowerOfTwo,
    TooLarge(usize),
    TooSmall(usize),
}

impl fmt::Display for FatfsNumericError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            FatfsNumericError::NotPowerOfTwo => write!(f, "not power of two"),
            FatfsNumericError::TooLarge(max) => write!(f, "value > {}", max),
            FatfsNumericError::TooSmall(min) => write!(f, "value < {}", min),
        }
    }
}

#[derive(fmt::Debug)]
/// Error type returned as the inner error for errors with ErrorKind::Other.
pub enum FatfsError {
    BackupBootSectorInvalid,
    BadDiskSize,
    ClusterFatMismatch,
    DirectoryNotEmpty,
    FileNameBadCharacter,
    FileNameEmpty,
    FileNameTooLong,
    FsInfoInvalid,
    InvalidBootSectorSig,
    InvalidBytesPerSector(FatfsNumericError),
    InvalidClusterNumber,
    InvalidFatType,
    InvalidFats,
    InvalidLeadSig,
    InvalidNumClusters,
    InvalidReservedSectors,
    InvalidSectorsPerCluster(FatfsNumericError),
    InvalidSectorsPerFat,
    InvalidStrucSig,
    InvalidTrailSig,
    IsDirectory,
    NoSpace,
    NonZeroRootEntries,
    NonZeroTotalSectors,
    NotDirectory,
    TooManyClusters,
    TooManySectors,
    TotalSectorsTooSmall,
    UnknownVersion,
    VolumeTooSmall,
    ZeroRootEntries,
    ZeroTotalSectors,
}

#[cfg(feature = "std")]
impl Error for FatfsError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        None
    }
}

impl fmt::Display for FatfsError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", String::from(self))
    }
}

impl From<FatfsError> for io::Error {
    fn from(error: FatfsError) -> io::Error {
        io::Error::new(io::ErrorKind::Other, error)
    }
}

impl From<&FatfsError> for String {
    fn from(error: &FatfsError) -> String {
        match error {
            FatfsError::BackupBootSectorInvalid => "Invalid BPB (backup boot-sector not in a reserved region)".to_owned(),
            FatfsError::BadDiskSize => "Cannot select FAT type - unfortunate disk size".to_owned(),
            FatfsError::ClusterFatMismatch => "Total number of clusters and FAT type does not match. Try other volume size".to_owned(),
            FatfsError::DirectoryNotEmpty => "Directory not empty".to_owned(),
            FatfsError::FileNameBadCharacter => "File name contains unsupported characters".to_owned(),
            FatfsError::FileNameEmpty => "File name is empty".to_owned(),
            FatfsError::FileNameTooLong => "File name too long".to_owned(),
            FatfsError::FsInfoInvalid => "Invalid BPB (FSInfo sector not in a reserved region)".to_owned(),
            FatfsError::InvalidBootSectorSig => "Invalid boot sector signature".to_owned(),
            FatfsError::InvalidBytesPerSector(what) => format!("Invalid bytes_per_sector value in BPB ({})", what),
            FatfsError::InvalidClusterNumber => "Cluster number is invalid".to_owned(),
            FatfsError::InvalidFatType => "Invalid FAT type".to_owned(),
            FatfsError::InvalidFats => "Invalid fats value in BPB".to_owned(),
            FatfsError::InvalidLeadSig => "Invalid lead_sig in FsInfo sector".to_owned(),
            FatfsError::InvalidNumClusters => "Invalid BPB (result of FAT32 determination from total number of clusters and sectors_per_fat_16 field differs)".to_owned(),
            FatfsError::InvalidReservedSectors => "Invalid reserved_sectors value in BPB".to_owned(),
            FatfsError::InvalidSectorsPerCluster(what) => format!("Invalid sectors_per_cluster value in BPB ({})", what),
            FatfsError::InvalidSectorsPerFat => "Invalid sectors_per_fat_32 value in BPB (should be non-zero for FAT32)".to_owned(),
            FatfsError::InvalidStrucSig => "Invalid struc_sig in FsInfo sector".to_owned(),
            FatfsError::InvalidTrailSig => "Invalid trail_sig in FsInfo sector".to_owned(),
            FatfsError::IsDirectory => "Is a directory".to_owned(),
            FatfsError::NoSpace => "No space left on device".to_owned(),
            FatfsError::NonZeroRootEntries => "Invalid root_entries value in BPB (should be zero for FAT32)".to_owned(),
            FatfsError::NonZeroTotalSectors => "Invalid BPB (total_sectors_16 or total_sectors_32 should be non-zero)".to_owned(),
            FatfsError::NotDirectory => "Not a directory".to_owned(),
            FatfsError::TooManyClusters => "Too many clusters".to_owned(),
            FatfsError::TooManySectors => "Volume has too many sectors".to_owned(),
            FatfsError::TotalSectorsTooSmall => "Invalid BPB (total_sectors field value is too small)".to_owned(),
            FatfsError::UnknownVersion => "Unknown FS version".to_owned(),
            FatfsError::VolumeTooSmall =>  "Volume is too small".to_owned(),
            FatfsError::ZeroRootEntries => "Empty root directory region defined in FAT12/FAT16 BPB".to_owned(),
            FatfsError::ZeroTotalSectors => "Invalid total_sectors_16 value in BPB (should be zero for FAT32)".to_owned(),
        }
    }
}

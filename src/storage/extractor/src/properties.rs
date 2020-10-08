// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub static DEFAULT_ALIGNMENT: u64 = 8192;

/// ExtentKind describes the type of the extent.
///
/// ExtentKind may mean different things based on the storage software.
/// ExtentKind priority is Unmapped<Unused<Data<Pii.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Debug)]
pub enum ExtentKind {
    /// Extent is Unmapped.
    ///
    /// For example,
    /// * In fvm based partitions/filesystem, unmapped may mean pslice does not exist for vslice.
    /// * In ftl, it may mean that the logical block is not mapped to a physical page
    Unmmapped,

    /// Extent is mapped but is not in use.
    ///
    /// For example,
    /// * In filesystem this extent maybe free block as indicated by a "bitmap"
    /// * In fvm this extent maybe a free slice.
    Unused,

    /// Extent contain `Data` that is pii free and can be extracted.
    ///
    /// `Data` itself doesn't mean it will be written to the image.
    Data,

    /// Extent contains data that is Pii.
    ///
    /// `Pii` itself doesn't mean extent data will not written to the image.
    Pii,
}

/// DataKind describes the type of the data within an extent.
/// DataKind priority is Skipped<Zeroes<Unmodified<Modified.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Debug)]
pub enum DataKind {
    /// Skipped dumping data for the extent.
    ///
    /// It maybe skipped because of various reasons like ExtentKind is
    /// {Unmapped, Unused, Pii} or it was skipped because storage software did not
    /// find the contents useful.
    Skipped,

    /// Skipped dumping extent data because it contained only zeroes.
    Zeroes,

    /// Dumped data is unmodified.
    Unmodified,

    /// Dumped data is modifed to obfuscate Pii.
    Modified,
}

/// Properties of an extent
///
/// extent_kind has higher priority than data_kind.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Debug)]
pub struct ExtentProperties {
    // Extent's type.
    pub extent_kind: ExtentKind,

    // Data type.
    pub data_kind: DataKind,
}

impl ExtentProperties {
    /// Returns `true` if the extent's data is considered PII.
    pub fn is_pii(&self) -> bool {
        self.extent_kind == ExtentKind::Pii
    }

    // Returns `true` if self has higher priority than the other.
    pub fn overrides(&self, other: &ExtentProperties) -> bool {
        self > other
    }
}

#[cfg(test)]
mod test {
    use crate::properties::{DataKind, ExtentKind, ExtentProperties};

    #[test]
    fn test_extent_kind_priority() {
        assert!(ExtentKind::Unmmapped < ExtentKind::Unused);
        assert!(ExtentKind::Unused < ExtentKind::Data);
        assert!(ExtentKind::Data < ExtentKind::Pii);
    }

    #[test]
    fn test_data_kind_priority() {
        assert!(DataKind::Skipped < DataKind::Zeroes);
        assert!(DataKind::Zeroes < DataKind::Unmodified);
        assert!(DataKind::Unmodified < DataKind::Modified);
    }

    #[test]
    fn test_extent_properties_priority() {
        let low_priority_extent_kind =
            ExtentProperties { extent_kind: ExtentKind::Unused, data_kind: DataKind::Unmodified };
        let high_priority =
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Modified };
        assert!(low_priority_extent_kind < high_priority);
        let low_priority_data_kind =
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };
        assert!(low_priority_data_kind < high_priority);
    }

    #[test]
    fn test_is_pii() {
        let p = ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Modified };
        assert!(p.is_pii());
    }

    #[test]
    fn test_not_pii() {
        let p = ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Modified };
        assert!(!p.is_pii());
    }

    #[test]
    fn test_overrides_extent_kind() {
        let p = ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Modified };
        let q = ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Modified };
        assert!(p.overrides(&q));
        assert!(!q.overrides(&p));
    }

    #[test]
    fn test_overrides_data_kind() {
        let p = ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Modified };
        let q = ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Zeroes };
        assert!(p.overrides(&q));
        assert!(!q.overrides(&p));
    }
}

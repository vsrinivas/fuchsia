// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Deserialization for `.font_pkgs.json` files.

use {
    crate::{
        merge::{TryMerge, TryMergeGroups},
        serde_ext::{self, LoadError},
    },
    anyhow::Error,
    rayon::prelude::*,
    serde_derive::Deserialize,
    std::{
        collections::BTreeMap,
        path::{Path, PathBuf},
    },
};

/// Possible versions of [FontPackageListing].
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
#[serde(tag = "version")]
enum FontPackageListingWrapper {
    #[serde(rename = "1")]
    Version1(FontPackageListingInternal),
}

/// A listing of font files and information about the GN packages to which they belong.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
struct FontPackageListingInternal {
    pub packages: Vec<FontPackageEntry>,
}

impl FontPackageListingInternal {
    /// Loads and merges multiple listings from files.
    fn load_from_paths<T, P>(paths: T) -> Result<Self, Error>
    where
        T: IntoIterator<Item = P>,
        P: AsRef<Path>,
    {
        let paths: Vec<PathBuf> =
            paths.into_iter().map(|path_ref| path_ref.as_ref().into()).collect();
        let font_pkgs: Result<Vec<Self>, _> =
            paths.par_iter().map(|path| Self::load_from_path(path)).collect();
        Self::try_merge(font_pkgs?)
    }

    /// Loads a single listing from a file.
    fn load_from_path<T: AsRef<Path>>(path: T) -> Result<Self, LoadError> {
        match serde_ext::load_from_path(path) {
            Ok(FontPackageListingWrapper::Version1(listing)) => Ok(listing),
            Err(err) => Err(err),
        }
    }

    /// Merges multiple listings into a single one, alphabetically sorted by file name.
    /// Exact duplicates are fine, but if there are any inconsistent definitions under the same
    /// file name, an error will result.
    fn try_merge<T>(listings: T) -> Result<Self, Error>
    where
        T: IntoIterator<Item = Self>,
    {
        let merged_entries: Vec<FontPackageEntry> =
            listings.into_iter().flat_map(|listing| listing.packages).try_merge_groups()?;

        Ok(FontPackageListingInternal { packages: merged_entries })
    }
}

/// An in-memory representation of a ".font_pkgs.json" file, with some indexing niceties.
pub(crate) struct FontPackageListing {
    by_name: BTreeMap<String, FontPackageEntry>,
}

impl FontPackageListing {
    /// Load multiple .font_pkgs.json files and merge them.
    pub fn load_from_paths<T, P>(paths: T) -> Result<Self, Error>
    where
        T: IntoIterator<Item = P>,
        P: AsRef<Path>,
    {
        let internal = FontPackageListingInternal::load_from_paths(paths)?;
        let mut by_name = BTreeMap::new();

        for package in internal.packages {
            by_name.insert(package.file_name.clone(), package);
        }

        Ok(FontPackageListing { by_name })
    }

    pub fn get(&self, asset_name: &str) -> Option<&FontPackageEntry> {
        self.by_name.get(asset_name)
    }

    pub fn get_safe_name(&self, asset_name: &str) -> Option<&str> {
        self.get(asset_name).map(|entry| &*entry.safe_name)
    }

    pub fn get_path_prefix(&self, asset_name: &str) -> Option<&Path> {
        self.get(asset_name).map(|entry| Path::new(&entry.path_prefix))
    }
}

/// A single entry in the font package listing.
#[derive(Clone, Debug, PartialEq, Eq, Hash, Deserialize)]
pub struct FontPackageEntry {
    /// Name of the font file, e.g. `"Roboto-Black.ttf"`
    pub file_name: String,
    /// Transformed name of the font file for use in Fuchsia package names, e.g.
    /// `"roboto-black-ttf"`.
    pub safe_name: String,
    /// Prefix of the path the font relative to the root font directory, e.g. `""` or
    /// `"roboto/"`.
    pub path_prefix: String,
}

impl FontPackageEntry {
    #[cfg(test)]
    pub fn new(
        file_name: impl AsRef<str>,
        safe_name: impl AsRef<str>,
        path_prefix: impl AsRef<str>,
    ) -> FontPackageEntry {
        FontPackageEntry {
            file_name: file_name.as_ref().to_string(),
            safe_name: safe_name.as_ref().to_string(),
            path_prefix: path_prefix.as_ref().to_string(),
        }
    }
}

impl TryMerge for FontPackageEntry {
    type Key = String;

    fn key(&self) -> Self::Key {
        self.file_name.clone()
    }

    fn has_matching_fields(&self, other: &Self) -> bool {
        return self.safe_name == other.safe_name && self.path_prefix == other.path_prefix;
    }

    fn try_merge_group(mut group: Vec<Self>) -> Result<Self, Error> {
        // They should all be identical, so just take the last one.
        Ok(group.pop().unwrap())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, pretty_assertions::assert_eq};

    #[test]
    fn test_merge() -> Result<(), Error> {
        let group = vec![
            FontPackageListingInternal {
                packages: vec![
                    FontPackageEntry::new("a.ttf", "a-ttf", ""),
                    FontPackageEntry::new("d.ttf", "d-ttf", ""),
                    FontPackageEntry::new("e.ttf", "e-ttf", ""),
                ],
            },
            FontPackageListingInternal {
                packages: vec![
                    FontPackageEntry::new("h.ttf", "h-ttf", ""),
                    FontPackageEntry::new("c.ttf", "c-ttf", ""),
                    FontPackageEntry::new("j.ttf", "j-ttf", ""),
                    FontPackageEntry::new("a.ttf", "a-ttf", ""),
                ],
            },
        ];
        let expected = FontPackageListingInternal {
            packages: vec![
                FontPackageEntry::new("a.ttf", "a-ttf", ""),
                FontPackageEntry::new("c.ttf", "c-ttf", ""),
                FontPackageEntry::new("d.ttf", "d-ttf", ""),
                FontPackageEntry::new("e.ttf", "e-ttf", ""),
                FontPackageEntry::new("h.ttf", "h-ttf", ""),
                FontPackageEntry::new("j.ttf", "j-ttf", ""),
            ],
        };
        assert_eq!(FontPackageListingInternal::try_merge(group)?, expected);
        Ok(())
    }

    #[test]
    fn test_merge_collision() {
        let group = vec![
            FontPackageListingInternal {
                packages: vec![
                    FontPackageEntry::new("a.ttf", "a-ttf", ""),
                    FontPackageEntry::new("d.ttf", "d-ttf", ""),
                    FontPackageEntry::new("e.ttf", "e-ttf", ""),
                ],
            },
            FontPackageListingInternal {
                packages: vec![
                    FontPackageEntry::new("h.ttf", "h-ttf", ""),
                    FontPackageEntry::new("a.ttf", "aa-ttf", ""),
                    FontPackageEntry::new("j.ttf", "j-ttf", ""),
                ],
            },
        ];

        assert!(FontPackageListingInternal::try_merge(group).is_err());
    }
}

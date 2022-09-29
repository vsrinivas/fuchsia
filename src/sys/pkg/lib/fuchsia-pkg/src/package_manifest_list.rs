// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::Serialize,
    std::{fmt, io::BufRead, iter::FromIterator, slice, vec},
};

/// [PackageManifestList] is a construct that points at a path that contains a
/// package manifest list. This will be used by the packaging tooling to
/// understand when packages have changed.
#[derive(Serialize)]
#[serde(transparent)]
pub struct PackageManifestList(Vec<String>);

impl PackageManifestList {
    /// Construct a new [PackageManifestList].
    pub fn new() -> Self {
        Self::from_vec(vec![])
    }

    pub fn from_vec(package_manifest_list: Vec<String>) -> Self {
        Self(package_manifest_list)
    }

    /// Push a package manifest path to the end of the [PackageManifestList].
    pub fn push(&mut self, package_manifest_path: String) {
        self.0.push(package_manifest_path);
    }

    /// Returns an iterator over the package manifest path entries.
    pub fn iter<'a>(&'a self) -> Iter<'a> {
        Iter(self.0.iter())
    }

    pub fn from_reader(reader: impl std::io::Read) -> Result<Self, std::io::Error> {
        let reader = std::io::BufReader::new(reader);
        let lines = reader.lines().collect::<Result<Vec<_>, _>>()?;
        Ok(Self(lines))
    }
}

impl IntoIterator for PackageManifestList {
    type Item = String;
    type IntoIter = IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        IntoIter(self.0.into_iter())
    }
}

impl From<Vec<String>> for PackageManifestList {
    fn from(package_manifest_list: Vec<String>) -> Self {
        Self::from_vec(package_manifest_list)
    }
}

impl fmt::Debug for PackageManifestList {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl FromIterator<String> for PackageManifestList {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = String>,
    {
        PackageManifestList(iter.into_iter().collect())
    }
}

/// Immutable iterator over the package manifest paths.
pub struct Iter<'a>(slice::Iter<'a, String>);

impl<'a> Iterator for Iter<'a> {
    type Item = &'a String;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next()
    }
}

/// An iterator that moves out of the [PackageManifestList].
pub struct IntoIter(vec::IntoIter<String>);

impl Iterator for IntoIter {
    type Item = String;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, serde_json::json};

    #[test]
    fn test_serialize() {
        let package_manifest_list = PackageManifestList::from_vec(vec![
            "obj/build/images/config-data/package_manifest.json".into(),
            "obj/build/images/shell-commands/package_manifest.json".into(),
            "obj/src/sys/component_index/component_index/package_manifest.json".into(),
            "obj/build/images/driver-manager-base-config/package_manifest.json".into(),
        ]);

        assert_eq!(
            serde_json::to_value(&package_manifest_list).unwrap(),
            json!([
                "obj/build/images/config-data/package_manifest.json",
                "obj/build/images/shell-commands/package_manifest.json",
                "obj/src/sys/component_index/component_index/package_manifest.json",
                "obj/build/images/driver-manager-base-config/package_manifest.json",
            ]),
        );
    }

    #[test]
    fn test_from_reader() {
        let raw_package_manifest_list = r#"obj/build/images/config-data/package_manifest.json
obj/build/images/shell-commands/package_manifest.json
obj/src/sys/component_index/component_index/package_manifest.json
obj/build/images/driver-manager-base-config/package_manifest.json"#;

        let package_manifest_list =
            PackageManifestList::from_reader(std::io::Cursor::new(raw_package_manifest_list))
                .unwrap();

        assert_eq!(
            package_manifest_list.iter().map(|s| s.as_str()).collect::<Vec<_>>(),
            vec![
                "obj/build/images/config-data/package_manifest.json",
                "obj/build/images/shell-commands/package_manifest.json",
                "obj/src/sys/component_index/component_index/package_manifest.json",
                "obj/build/images/driver-manager-base-config/package_manifest.json",
            ]
        );
    }

    #[test]
    fn test_iter() {
        let package_manifest_list = PackageManifestList::from_vec(vec![
            "obj/build/images/config-data/package_manifest.json".into(),
            "obj/build/images/shell-commands/package_manifest.json".into(),
            "obj/src/sys/component_index/component_index/package_manifest.json".into(),
            "obj/build/images/driver-manager-base-config/package_manifest.json".into(),
        ]);

        assert_eq!(
            package_manifest_list.iter().map(|s| s.as_str()).collect::<Vec<_>>(),
            vec![
                "obj/build/images/config-data/package_manifest.json",
                "obj/build/images/shell-commands/package_manifest.json",
                "obj/src/sys/component_index/component_index/package_manifest.json",
                "obj/build/images/driver-manager-base-config/package_manifest.json",
            ]
        );
    }

    #[test]
    fn test_into_iter() {
        let entries = vec![
            "obj/build/images/config-data/package_manifest.json".into(),
            "obj/build/images/shell-commands/package_manifest.json".into(),
            "obj/src/sys/component_index/component_index/package_manifest.json".into(),
            "obj/build/images/driver-manager-base-config/package_manifest.json".into(),
        ];
        let package_manifest_list = PackageManifestList::from_vec(entries.clone());

        assert_eq!(package_manifest_list.into_iter().collect::<Vec<_>>(), entries,);
    }
}

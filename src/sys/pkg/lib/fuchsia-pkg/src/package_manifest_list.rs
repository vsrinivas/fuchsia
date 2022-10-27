// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    camino::Utf8PathBuf,
    serde::Serialize,
    std::{fmt, io::BufRead, iter::FromIterator, slice, vec},
};

/// [PackageManifestList] is a construct that points at a path that contains a
/// package manifest list. This will be used by the packaging tooling to
/// understand when packages have changed.
#[derive(Serialize)]
#[serde(transparent)]
pub struct PackageManifestList(Vec<Utf8PathBuf>);

impl PackageManifestList {
    /// Construct a new [PackageManifestList].
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        Self(vec![])
    }

    /// Push a package manifest path to the end of the [PackageManifestList].
    pub fn push(&mut self, package_manifest_path: Utf8PathBuf) {
        self.0.push(package_manifest_path);
    }

    /// Returns an iterator over the package manifest path entries.
    pub fn iter(&self) -> Iter<'_> {
        Iter(self.0.iter())
    }

    pub fn from_reader(reader: impl std::io::Read) -> Result<Self, std::io::Error> {
        let reader = std::io::BufReader::new(reader);
        let lines = reader
            .lines()
            .map(|line| line.map(Utf8PathBuf::from))
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self(lines))
    }

    /// Write the package list manifest to this path.
    pub fn to_writer(&self, mut writer: impl std::io::Write) -> Result<(), std::io::Error> {
        for package_manifest_path in &self.0 {
            writeln!(writer, "{}", package_manifest_path)?;
        }
        Ok(())
    }
}

impl IntoIterator for PackageManifestList {
    type Item = Utf8PathBuf;
    type IntoIter = IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        IntoIter(self.0.into_iter())
    }
}

impl From<Vec<Utf8PathBuf>> for PackageManifestList {
    fn from(package_manifest_list: Vec<Utf8PathBuf>) -> Self {
        Self(package_manifest_list)
    }
}

impl fmt::Debug for PackageManifestList {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl FromIterator<Utf8PathBuf> for PackageManifestList {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = Utf8PathBuf>,
    {
        PackageManifestList(iter.into_iter().collect())
    }
}

/// Immutable iterator over the package manifest paths.
pub struct Iter<'a>(slice::Iter<'a, Utf8PathBuf>);

impl<'a> Iterator for Iter<'a> {
    type Item = &'a Utf8PathBuf;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next()
    }
}

/// An iterator that moves out of the [PackageManifestList].
pub struct IntoIter(vec::IntoIter<Utf8PathBuf>);

impl Iterator for IntoIter {
    type Item = Utf8PathBuf;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, serde_json::json};

    #[test]
    fn test_serialize() {
        let package_manifest_list = PackageManifestList::from(vec![
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
    fn test_to_writer() {
        let package_manifest_list = PackageManifestList::from(vec![
            "obj/build/images/config-data/package_manifest.json".into(),
            "obj/build/images/shell-commands/package_manifest.json".into(),
            "obj/src/sys/component_index/component_index/package_manifest.json".into(),
            "obj/build/images/driver-manager-base-config/package_manifest.json".into(),
        ]);

        let mut out = vec![];
        package_manifest_list.to_writer(&mut out).unwrap();

        assert_eq!(
            String::from_utf8(out).unwrap(),
            "obj/build/images/config-data/package_manifest.json\n\
            obj/build/images/shell-commands/package_manifest.json\n\
            obj/src/sys/component_index/component_index/package_manifest.json\n\
            obj/build/images/driver-manager-base-config/package_manifest.json\n"
        );
    }

    #[test]
    fn test_iter() {
        let package_manifest_list = PackageManifestList::from(vec![
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
        let package_manifest_list = PackageManifestList::from(entries.clone());

        assert_eq!(package_manifest_list.into_iter().collect::<Vec<_>>(), entries,);
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::MetaPackage,
    anyhow::Result,
    camino::{Utf8Path, Utf8PathBuf},
    fuchsia_merkle::Hash,
    fuchsia_url::RelativePackageUrl,
    serde::{de::Deserializer, Deserialize, Serialize},
    std::{fs, io},
};

/// Helper type for reading the subpackage manifest.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SubpackagesManifest(SubpackagesManifestV0);

impl SubpackagesManifest {
    /// Return the subpackage manifest entries.
    pub fn entries(&self) -> &[SubpackagesManifestEntry] {
        &self.0.entries
    }

    /// Open up each entry in the manifest and return the subpackage url and hash.
    pub fn to_subpackages(&self) -> Result<Vec<(RelativePackageUrl, Hash)>> {
        let mut entries = Vec::with_capacity(self.0.entries.len());
        for entry in &self.0.entries {
            let url = match &entry.kind {
                SubpackagesManifestEntryKind::Url(url) => url.clone(),
                SubpackagesManifestEntryKind::File(path) => {
                    let f = fs::File::open(path)?;
                    let meta_package = MetaPackage::deserialize(io::BufReader::new(f))?;
                    meta_package.name().clone().into()
                }
            };

            // The merkle file is a hex encoded string.
            let merkle = fs::read_to_string(&entry.merkle_file)?;
            let package_hash = merkle.parse()?;

            entries.push((url, package_hash));
        }
        Ok(entries)
    }

    /// Deserializes a `SubpackagesManifest` from json.
    pub fn deserialize(reader: impl io::BufRead) -> Result<Self> {
        Ok(SubpackagesManifest(serde_json::from_reader(reader)?))
    }

    /// Serializes a `SubpackagesManifest` to json.
    pub fn serialize(&self, writer: impl io::Write) -> Result<()> {
        Ok(serde_json::to_writer(writer, &self.0.entries)?)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct SubpackagesManifestV0 {
    entries: Vec<SubpackagesManifestEntry>,
}

impl From<Vec<SubpackagesManifestEntry>> for SubpackagesManifest {
    fn from(entries: Vec<SubpackagesManifestEntry>) -> Self {
        SubpackagesManifest(SubpackagesManifestV0 { entries })
    }
}

impl<'de> Deserialize<'de> for SubpackagesManifestV0 {
    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        use serde::de::Error as _;

        #[derive(Deserialize)]
        struct Helper {
            #[serde(default)]
            name: Option<RelativePackageUrl>,
            #[serde(default)]
            meta_package_file: Option<Utf8PathBuf>,
            merkle_file: Utf8PathBuf,
        }

        let manifest_entries = Vec::<Helper>::deserialize(deserializer)?;

        let mut entries = vec![];
        for Helper { name, meta_package_file, merkle_file } in manifest_entries {
            let kind = match (name, meta_package_file) {
                (Some(name), _) => SubpackagesManifestEntryKind::Url(name),
                (None, Some(meta_package_file)) => {
                    SubpackagesManifestEntryKind::File(meta_package_file)
                }
                (None, None) => {
                    return Err(D::Error::missing_field(
                        "missing entry for `name` or `meta_package_file`",
                    ))
                }
            };

            entries.push(SubpackagesManifestEntry { kind, merkle_file });
        }

        Ok(SubpackagesManifestV0 { entries })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SubpackagesManifestEntry {
    kind: SubpackagesManifestEntryKind,
    merkle_file: Utf8PathBuf,
}

impl Serialize for SubpackagesManifestEntry {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        #[derive(Serialize)]
        struct Helper<'a> {
            #[serde(skip_serializing_if = "Option::is_none")]
            name: Option<&'a RelativePackageUrl>,
            #[serde(skip_serializing_if = "Option::is_none")]
            meta_package_file: Option<&'a Utf8PathBuf>,
            merkle_file: &'a Utf8PathBuf,
        }
        let mut helper =
            Helper { name: None, meta_package_file: None, merkle_file: &self.merkle_file };
        match &self.kind {
            SubpackagesManifestEntryKind::Url(url) => helper.name = Some(url),
            SubpackagesManifestEntryKind::File(path) => helper.meta_package_file = Some(path),
        }
        helper.serialize(serializer)
    }
}

impl SubpackagesManifestEntry {
    /// Construct a new [SubpackagesManifestEntry].
    pub fn new(kind: SubpackagesManifestEntryKind, merkle_file: Utf8PathBuf) -> Self {
        Self { kind, merkle_file }
    }

    /// Returns the subpackages manifest entry's [EntryKind].
    pub fn kind(&self) -> &SubpackagesManifestEntryKind {
        &self.kind
    }

    /// Returns the subpackages manifest entry's merkle file.
    pub fn merkle_file(&self) -> &Utf8Path {
        &self.merkle_file
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SubpackagesManifestEntryKind {
    Url(RelativePackageUrl),
    File(Utf8PathBuf),
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        camino::Utf8Path,
        fuchsia_url::PackageName,
        serde_json::json,
        std::{fs::File, io},
    };

    #[test]
    fn test_deserialize() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Generate a subpackages manifest.
        let pkg1_name = PackageName::try_from("pkg1".to_string()).unwrap();
        let pkg1_url = RelativePackageUrl::from(pkg1_name.clone());
        let pkg1_hash = fuchsia_merkle::from_slice(b"pkg1").root();
        let pkg1_merkle_file = dir.join("pkg1-merkle");

        let pkg2_name = PackageName::try_from("pkg2".to_string()).unwrap();
        let pkg2_url = RelativePackageUrl::from(pkg2_name.clone());
        let pkg2_hash = fuchsia_merkle::from_slice(b"pkg2").root();
        let pkg2_meta_package_file = dir.join("pkg2-meta-package");
        let pkg2_merkle_file = dir.join("pkg2-merkle");

        // Write out all the files.
        MetaPackage::from_name(pkg2_name)
            .serialize(File::create(&pkg2_meta_package_file).unwrap())
            .unwrap();

        std::fs::write(&pkg1_merkle_file, pkg1_hash.to_string().as_bytes()).unwrap();
        std::fs::write(&pkg2_merkle_file, pkg2_hash.to_string().as_bytes()).unwrap();

        // Make sure we can deserialize from the manifest format.
        let manifest_path = dir.join("subpackages-manifest");
        serde_json::to_writer(
            File::create(&manifest_path).unwrap(),
            &json!([
                {
                    "name": pkg1_name.to_string(),
                    "merkle_file": pkg1_merkle_file.to_string(),
                },
                {
                    "meta_package_file": pkg2_meta_package_file.to_string(),
                    "merkle_file": pkg2_merkle_file.to_string(),
                },
            ]),
        )
        .unwrap();

        let manifest = SubpackagesManifest::deserialize(io::BufReader::new(
            File::open(&manifest_path).unwrap(),
        ))
        .unwrap();

        assert_eq!(
            manifest.0.entries,
            vec![
                SubpackagesManifestEntry {
                    kind: SubpackagesManifestEntryKind::Url(pkg1_url.clone()),
                    merkle_file: pkg1_merkle_file
                },
                SubpackagesManifestEntry {
                    kind: SubpackagesManifestEntryKind::File(pkg2_meta_package_file),
                    merkle_file: pkg2_merkle_file,
                },
            ]
        );

        // Make sure we can convert the manifest into subpackages.
        assert_eq!(
            manifest.to_subpackages().unwrap(),
            vec![(pkg1_url, pkg1_hash), (pkg2_url, pkg2_hash)]
        );
    }

    #[test]
    fn test_meta_package_not_found() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let pkg_meta_package_file = dir.join("pkg-meta-package");
        let pkg_hash = fuchsia_merkle::from_slice(b"pkg").root();
        let pkg_merkle_file = dir.join("merkle");
        std::fs::write(&pkg_merkle_file, pkg_hash.to_string().as_bytes()).unwrap();

        let manifest_path = dir.join("subpackages-manifest");
        serde_json::to_writer(
            File::create(&manifest_path).unwrap(),
            &json!([
                {
                    "meta_package_file": pkg_meta_package_file.to_string(),
                    "merkle_file": pkg_merkle_file.to_string(),
                },
            ]),
        )
        .unwrap();

        let manifest = SubpackagesManifest::deserialize(io::BufReader::new(
            File::open(&manifest_path).unwrap(),
        ))
        .unwrap();

        // We should error out if the merkle file doesn't exist.
        assert_matches!(
            manifest.to_subpackages(),
            Err(err) if err.downcast_ref::<io::Error>().unwrap().kind() == io::ErrorKind::NotFound
        );

        // It should work once we write the file.
        let pkg_name = PackageName::try_from("pkg".to_string()).unwrap();
        MetaPackage::from_name(pkg_name.clone())
            .serialize(File::create(&pkg_meta_package_file).unwrap())
            .unwrap();
        let pkg_url = RelativePackageUrl::from(pkg_name);

        assert_eq!(manifest.to_subpackages().unwrap(), vec![(pkg_url, pkg_hash)]);
    }

    #[test]
    fn test_merkle_file_not_found() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let pkg_name = PackageName::try_from("pkg".to_string()).unwrap();
        let pkg_url = RelativePackageUrl::from(pkg_name);
        let pkg_hash = fuchsia_merkle::from_slice(b"pkg").root();
        let pkg_merkle_file = dir.join("merkle");

        let manifest_path = dir.join("subpackages-manifest");
        serde_json::to_writer(
            File::create(&manifest_path).unwrap(),
            &json!([
                {
                    "name": pkg_url.to_string(),
                    "merkle_file": pkg_merkle_file.to_string(),
                },
            ]),
        )
        .unwrap();

        let manifest = SubpackagesManifest::deserialize(io::BufReader::new(
            File::open(&manifest_path).unwrap(),
        ))
        .unwrap();

        // We should error out if the merkle file doesn't exist.
        assert_matches!(
            manifest.to_subpackages(),
            Err(err) if err.downcast_ref::<io::Error>().unwrap().kind() == io::ErrorKind::NotFound
        );

        // It should work once we write the file.
        std::fs::write(&pkg_merkle_file, pkg_hash.to_string().as_bytes()).unwrap();
        assert_eq!(manifest.to_subpackages().unwrap(), vec![(pkg_url, pkg_hash)]);
    }

    #[test]
    fn test_serialize() {
        let entries = vec![
            SubpackagesManifestEntry::new(
                SubpackagesManifestEntryKind::Url("subpackage-name".parse().unwrap()),
                "merkle-path-0".into(),
            ),
            SubpackagesManifestEntry::new(
                SubpackagesManifestEntryKind::File("file-path".into()),
                "merkle-path-1".into(),
            ),
        ];
        let manifest = SubpackagesManifest::from(entries);

        let mut bytes = vec![];
        let () = manifest.serialize(&mut bytes).unwrap();
        let actual_json: serde_json::Value = serde_json::from_slice(&bytes).unwrap();

        assert_eq!(
            actual_json,
            json!([
                {
                    "name": "subpackage-name",
                    "merkle_file": "merkle-path-0"
                },
                {
                    "meta_package_file": "file-path",
                    "merkle_file": "merkle-path-1"
                },
            ])
        );
    }

    #[test]
    fn test_serialize_deserialize() {
        let entries = vec![
            SubpackagesManifestEntry::new(
                SubpackagesManifestEntryKind::Url("subpackage-name".parse().unwrap()),
                "merkle-path-0".into(),
            ),
            SubpackagesManifestEntry::new(
                SubpackagesManifestEntryKind::File("file-path".into()),
                "merkle-path-1".into(),
            ),
        ];
        let manifest = SubpackagesManifest::from(entries);

        let mut bytes = vec![];
        let () = manifest.serialize(&mut bytes).unwrap();
        let deserialized =
            SubpackagesManifest::deserialize(io::BufReader::new(bytes.as_slice())).unwrap();

        assert_eq!(deserialized, manifest);
    }
}

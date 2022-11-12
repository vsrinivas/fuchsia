// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{artifact::ArtifactReader, io::ReadSeek, key_value::parse_key_value},
    anyhow::{anyhow, Context, Result},
    fuchsia_archive::Utf8Reader,
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_url::{PackageName, PackageVariant},
    serde::{
        de::{self, Deserializer, Error as _, MapAccess, Visitor},
        ser::Serializer,
    },
    std::{
        collections::HashMap,
        fmt,
        fs::File,
        path::Path,
        str::{from_utf8, FromStr},
    },
};

/// Path within a Fuchsia package that contains the package contents manifest.
pub static META_CONTENTS_PATH: &str = "meta/contents";

pub fn open_update_package<P: AsRef<Path>>(
    update_package_path: P,
    artifact_reader: &mut Box<dyn ArtifactReader>,
) -> Result<Utf8Reader<Box<dyn ReadSeek>>> {
    let update_package_path = update_package_path.as_ref();
    let mut update_package_file = File::open(update_package_path).with_context(|| {
        format!("Failed to open update package meta.far at {:?}", update_package_path)
    })?;
    let update_package_hash = MerkleTree::from_reader(&mut update_package_file)
        .with_context(|| {
            format!(
                "Failed to compute merkle root of update package meta.far at {:?}",
                update_package_path
            )
        })?
        .root()
        .to_string();
    let far = artifact_reader.open(&Path::new(&update_package_hash)).with_context(|| {
        format!(
            "Failed to open update package meta.far at {:?} from artifact archives",
            update_package_path
        )
    })?;
    Utf8Reader::new(far).with_context(|| {
        format!(
            "Failed to initialize far reader for update package at {:?} with merkle root {}",
            update_package_path, update_package_hash
        )
    })
}

pub fn read_content_blob(
    far_reader: &mut Utf8Reader<impl ReadSeek>,
    artifact_reader: &mut Box<dyn ArtifactReader>,
    path: &str,
) -> Result<Vec<u8>> {
    let meta_contents = far_reader
        .read_file(META_CONTENTS_PATH)
        .context("Failed to read meta/contents from package")?;
    let meta_contents = from_utf8(meta_contents.as_slice())
        .context("Failed to convert package meta/contents from bytes to string")?;
    let paths_to_merkles = parse_key_value(meta_contents)
        .context("Failed to parse path=merkle pairs in package meta/contents file")?;
    let merkle_root = paths_to_merkles
        .get(path)
        .ok_or_else(|| anyhow!("Package does not contain file: {}", path))?;
    artifact_reader
        .read_bytes(&Path::new(merkle_root))
        .with_context(|| format!("Failed to load file from package: {}", path))
}

/// Package index files contain lines of the form:
/// [pkg-name-variant-path]=[merkle-root-hash].
pub type PackageIndexContents = HashMap<(PackageName, Option<PackageVariant>), Hash>;

/// Serialize package indices listing contents. A custom strategy is necessary because
/// map keys are stored as `(PackageName, Option<PackageVariant>)`, which must be manually converted
/// to a string representation.
pub fn serialize_pkg_index<S>(
    pkgs: &Option<PackageIndexContents>,
    serializer: S,
) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    match pkgs {
        None => serializer.serialize_none(),
        Some(pkgs) => {
            let mut map = HashMap::new();
            for ((name, variant), hash) in pkgs {
                match variant {
                    None => {
                        map.insert(name.to_string(), hash.to_string());
                    }
                    Some(variant) => {
                        map.insert(
                            format!("{}/{}", name.as_ref(), variant.as_ref()),
                            hash.to_string(),
                        );
                    }
                }
            }
            serializer.serialize_some(&map)
        }
    }
}

/// Deserialize package indices listing contents. A custom strategy is necessary because
/// map keys are stored as `(PackageName, Option<PackageVariant>)`, which must be manually converted
/// from a string representation.
pub fn deserialize_pkg_index<'de, D>(
    deserializer: D,
) -> Result<Option<PackageIndexContents>, D::Error>
where
    D: Deserializer<'de>,
{
    struct OptVisitor;
    struct MapVisitor;

    impl<'de> Visitor<'de> for OptVisitor {
        type Value = Option<PackageIndexContents>;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("optional pkgs map")
        }

        fn visit_none<E>(self) -> Result<Self::Value, E>
        where
            E: de::Error,
        {
            Ok(None)
        }

        fn visit_some<D>(self, deserializer: D) -> Result<Self::Value, D::Error>
        where
            D: Deserializer<'de>,
        {
            let visitor = MapVisitor;
            Ok(Some(deserializer.deserialize_any(visitor)?))
        }
    }

    impl<'de> Visitor<'de> for MapVisitor {
        type Value = PackageIndexContents;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("pkg index map")
        }

        fn visit_map<A>(self, mut map_access: A) -> Result<Self::Value, A::Error>
        where
            A: MapAccess<'de>,
        {
            let mut map = HashMap::with_capacity(map_access.size_hint().unwrap_or(0));
            loop {
                let entry: Option<(String, String)> = map_access.next_entry()?;
                match entry {
                    None => break,
                    Some((name_variant_string, hash_string)) => {
                        let mut name_parts = vec![];
                        let mut variant_part = None;
                        for part in name_variant_string.split("/") {
                            if let Some(prev_tail) = variant_part {
                                name_parts.push(prev_tail);
                            }
                            variant_part = Some(part);
                        }
                        let name_string = name_parts.join("/");
                        let name = PackageName::from_str(&name_string).map_err(|err| {
                            A::Error::custom(&format!(
                                "Failed to parse package name from string: {}: {}",
                                name_string, err
                            ))
                        })?;
                        let variant = variant_part
                            .map(|variant| {
                                PackageVariant::from_str(variant).map_err(|err| {
                                    A::Error::custom(&format!(
                                        "Failed to parse package variant from string: {}: {}",
                                        variant, err
                                    ))
                                })
                            })
                            .map_or(Ok(None), |r| r.map(Some))?;
                        let hash = Hash::from_str(&hash_string).map_err(|err| {
                            A::Error::custom(&format!(
                                "Failed to parse package hash from string: {}: {}",
                                hash_string, err
                            ))
                        })?;
                        map.insert((name, variant), hash);
                    }
                }
            }
            Ok(map)
        }
    }

    let visitor = OptVisitor;
    deserializer.deserialize_option(visitor)
}

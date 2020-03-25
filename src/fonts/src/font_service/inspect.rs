// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

use {
    super::{
        family::{FamilyInspectData, FamilyOrAlias},
        typeface::{Collection as TypefaceCollection, Typeface, TypefaceInspectData},
        AssetCollection, AssetId, AssetLoader,
    },
    fuchsia_inspect::{Node, StringProperty},
    std::{
        collections::{BTreeMap, HashMap},
        sync::Arc,
    },
    unicase::UniCase,
};

/// Inspect data for the font service as a whole, meant to be part of `FontService`.
///
/// This directly holds data about manifests, font families, and the fallback chain. (The
/// `AssetCollection` holds its own Inspect data.)
#[derive(Debug)]
pub struct ServiceInspectData {
    /// Top node for data about loaded font manifests.
    manifests_node: Node,
    /// List of font manifest file paths.
    manifests: Vec<StringProperty>,

    /// Top node for data about all loaded font families.
    families_node: Node,
    /// Data about each loaded font families.
    families: Vec<FamilyInspectData>,

    /// Top node for data about the fallback collection.
    fallback_collection_node: Node,
    /// Ordered list of data about all typefaces in the fallback collection.
    fallback_typefaces: Vec<TypefaceInspectData>,
}

type TypefaceLookup = HashMap<Arc<Typeface>, String>;

impl ServiceInspectData {
    /// Creates a new `ServiceInspectData`.
    pub fn new<L: AssetLoader>(
        root: &Node,
        manifest_paths: Vec<String>,
        assets: &AssetCollection<L>,
        families: &BTreeMap<UniCase<String>, FamilyOrAlias>,
        fallback_collection: &TypefaceCollection,
    ) -> Self {
        let asset_location_lookup = move |asset_id: AssetId| assets.get_asset_path_or_url(asset_id);

        let (manifests_node, manifests) = Self::make_manifests_data(root, manifest_paths);
        let (families_node, families, typeface_lookup) =
            Self::make_families_data(root, families, &asset_location_lookup);
        let (fallback_collection_node, fallback_typefaces) = Self::make_fallback_data(
            root,
            fallback_collection,
            &typeface_lookup,
            &asset_location_lookup,
        );

        ServiceInspectData {
            manifests_node,
            manifests,
            families_node,
            families,
            fallback_collection_node,
            fallback_typefaces,
        }
    }

    /// Creates a `manifests` node with a list of manifests.
    fn make_manifests_data(
        root: &Node,
        manifest_paths: Vec<String>,
    ) -> (Node, Vec<StringProperty>) {
        let manifests_node = root.create_child("manifests");
        let manifest_count = manifest_paths.len();
        let manifests: Vec<StringProperty> = manifest_paths
            .iter()
            .enumerate()
            .map(|(idx, manifest_str)| {
                manifests_node.create_string(&zero_pad(idx, manifest_count), manifest_str)
            })
            .collect();
        assert!(!manifests.is_empty());
        (manifests_node, manifests)
    }

    /// Creates a `families` node with a list of font families and their aliases and typefaces.
    fn make_families_data(
        root: &Node,
        families: &BTreeMap<UniCase<String>, FamilyOrAlias>,
        asset_location_lookup: &impl Fn(AssetId) -> Option<String>,
    ) -> (Node, Vec<FamilyInspectData>, TypefaceLookup) {
        let mut typeface_lookup: HashMap<Arc<Typeface>, String> = HashMap::new();

        let families_node = root.create_child("families");

        let mut family_datas: BTreeMap<String, FamilyInspectData> = BTreeMap::new();

        for (key, val) in families {
            if let FamilyOrAlias::Family(family) = val {
                let family_data =
                    FamilyInspectData::new(&families_node, family, &asset_location_lookup);
                family_datas.insert(key.to_string(), family_data);

                for typeface in &family.faces.faces {
                    typeface_lookup.insert(typeface.clone(), family.name.clone());
                }
            }
        }

        for (key, val) in families {
            if let FamilyOrAlias::Alias(family_name, overrides) = val {
                family_datas
                    .get_mut(family_name.as_ref())
                    .map(|family_data| family_data.add_alias(key.as_ref(), overrides));
            }
        }

        let families: Vec<FamilyInspectData> =
            family_datas.into_iter().map(|(_, family_data)| family_data).collect();

        (families_node, families, typeface_lookup)
    }

    /// Creates a `fallback_typefaces` node with a list of the typefaces in the fallback chain.
    fn make_fallback_data(
        root: &Node,
        fallback_collection: &TypefaceCollection,
        typeface_lookup: &TypefaceLookup,
        asset_location_lookup: &impl Fn(AssetId) -> Option<String>,
    ) -> (Node, Vec<TypefaceInspectData>) {
        let fallback_collection_node = root.create_child("fallback_typefaces");
        let fallback_faces_count = fallback_collection.faces.len();

        let fallback_typefaces = fallback_collection
            .faces
            .iter()
            .enumerate()
            .map(|(idx, typeface)| {
                TypefaceInspectData::with_numbered_node_name(
                    &fallback_collection_node,
                    idx,
                    fallback_faces_count,
                    typeface,
                    &asset_location_lookup,
                )
                .with_family_name(typeface_lookup.get(typeface).unwrap())
            })
            .collect();

        (fallback_collection_node, fallback_typefaces)
    }
}

/// Formats a number with a number of leading zeroes that ensures that all values up to `max_val`
/// would be displayed in the correct order when sorted as strings.
///
/// For example, if `max_val = 99`, then formats `5` as `"05"`
pub(crate) fn zero_pad(val: usize, max_val: usize) -> String {
    assert!(max_val >= val);
    let width: usize = ((max_val + 1) as f32).log10().ceil() as usize;
    format!("{:0width$}", val, width = width)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::font_service::{
            asset::{AssetCollectionBuilder, AssetCollectionError},
            family::FontFamilyBuilder,
            typeface::TypefaceCollectionBuilder,
        },
        anyhow::Error,
        async_trait::async_trait,
        char_collection::char_collect,
        fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width},
        fidl_fuchsia_io as io, fidl_fuchsia_mem as mem,
        fuchsia_inspect::{self as finspect, assert_inspect_tree},
        fuchsia_url::pkg_url::PkgUrl,
        manifest::v2,
        maplit::btreemap,
        std::path::Path,
        unicase::UniCase,
    };

    #[test]
    fn test_zero_pad() {
        assert_eq!(zero_pad(0, 5), "0".to_string());
        assert_eq!(zero_pad(13, 99), "13".to_string());
        assert_eq!(zero_pad(13, 100), "013".to_string());
        assert_eq!(zero_pad(100, 100), "100".to_string());
    }

    struct FakeAssetLoader {}

    #[async_trait]
    #[allow(unused_variables, dead_code)]
    impl AssetLoader for FakeAssetLoader {
        async fn fetch_package_directory(
            &self,
            package_locator: &v2::PackageLocator,
        ) -> Result<io::DirectoryProxy, AssetCollectionError> {
            unimplemented!()
        }

        fn load_vmo_from_path(&self, path: &Path) -> Result<mem::Buffer, AssetCollectionError> {
            unimplemented!()
        }

        async fn load_vmo_from_directory_proxy(
            &self,
            directory_proxy: io::DirectoryProxy,
            package_locator: &v2::PackageLocator,
            file_name: &str,
        ) -> Result<mem::Buffer, AssetCollectionError> {
            unimplemented!()
        }
    }

    #[allow(unused_variables)]
    #[test]
    fn test_service_inspect_data() -> Result<(), Error> {
        let inspector = finspect::Inspector::new();

        let manifest_paths = vec![
            "/path/to/manifest-a.font_manifest.json".to_string(),
            "/path/to/manifest-b.font_manifest.json".to_string(),
        ];

        let assets = vec![
            v2::Asset {
                file_name: "Alpha-Regular.ttf".to_string(),
                location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                    directory: "/config/data/assets".into(),
                }),
                typefaces: vec![],
            },
            v2::Asset {
                file_name: "Beta-Regular.ttf".to_string(),
                location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                    directory: "/config/data/assets".into(),
                }),
                typefaces: vec![],
            },
            v2::Asset {
                file_name: "Alpha-Condensed.ttf".to_string(),
                location: v2::AssetLocation::Package(v2::PackageLocator {
                    url: PkgUrl::parse(
                        "fuchsia-pkg://fuchsia.com/font-package-alpha-condensed-ttf",
                    )?,
                }),
                typefaces: vec![],
            },
        ];
        let cache_capacity_bytes = 5000;

        let asset_loader = FakeAssetLoader {};
        let mut asset_collection =
            AssetCollectionBuilder::new(asset_loader, cache_capacity_bytes, inspector.root());
        for asset in &assets {
            asset_collection.add_or_get_asset_id(asset);
        }
        let asset_collection = asset_collection.build();

        let typefaces = vec![
            Arc::new(Typeface {
                asset_id: AssetId(0),
                font_index: 0,
                slant: Slant::Upright,
                weight: 400,
                width: Width::Normal,
                languages: Default::default(),
                char_set: char_collect!(0x0..=0xDD).into(),
                generic_family: Some(GenericFontFamily::SansSerif),
            }),
            Arc::new(Typeface {
                asset_id: AssetId(1),
                font_index: 0,
                slant: Slant::Upright,
                weight: 400,
                width: Width::Normal,
                languages: Default::default(),
                char_set: char_collect!(0x0..=0xDD).into(),
                generic_family: Some(GenericFontFamily::Cursive),
            }),
            Arc::new(Typeface {
                asset_id: AssetId(2),
                font_index: 0,
                slant: Slant::Upright,
                weight: 400,
                width: Width::Condensed,
                languages: Default::default(),
                char_set: char_collect!(0x0..=0xDD).into(),
                generic_family: Some(GenericFontFamily::SansSerif),
            }),
        ];

        let mut family_a = FontFamilyBuilder::new("Alpha", Some(GenericFontFamily::SansSerif));
        family_a.add_typeface_once(typefaces[0].clone());
        family_a.add_typeface_once(typefaces[2].clone());
        let family_a = family_a.build();

        let mut family_b = FontFamilyBuilder::new("Beta", Some(GenericFontFamily::Cursive));
        family_b.add_typeface_once(typefaces[1].clone());
        let family_b = family_b.build();

        let families = btreemap! {
            UniCase::new("Alpha".to_string()) => FamilyOrAlias::Family(family_a),
            UniCase::new("Beta".to_string()) => FamilyOrAlias::Family(family_b),
            UniCase::new("Alef".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Alpha".to_string()), None),
            UniCase::new("Bet".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Beta".to_string()), None)
        };

        let mut fallback_collection = TypefaceCollectionBuilder::new();
        fallback_collection.add_typeface_once(typefaces[0].clone());
        fallback_collection.add_typeface_once(typefaces[2].clone());
        fallback_collection.add_typeface_once(typefaces[1].clone());
        let fallback_collection = fallback_collection.build();

        let _inspect_data = ServiceInspectData::new(
            inspector.root(),
            manifest_paths,
            &asset_collection,
            &families,
            &fallback_collection,
        );

        // Testing overall structure and order. Details are covered in more granular unit tests.
        assert_inspect_tree!(inspector, root: {
            manifests: {
                "0": "/path/to/manifest-a.font_manifest.json",
                "1": "/path/to/manifest-b.font_manifest.json",
            },
            asset_collection: {
                assets: {
                    "0": {
                        caching: "uncached",
                        location: "/config/data/assets/Alpha-Regular.ttf",
                    },
                    "1": {
                        caching: "uncached",
                        location: "/config/data/assets/Beta-Regular.ttf",
                    },
                    "2": {
                        caching: "uncached",
                        location: "fuchsia-pkg://fuchsia.com/font-package-alpha-condensed-ttf#Alpha-Condensed.ttf",
                    },
                },
                asset_cache: contains {},
                count: 3u64
            },
            families: {
                Alpha: contains {
                    aliases: {
                        "Alef": {}
                    }
                },
                Beta: contains {
                    aliases: {
                        "Bet": {}
                    }
                }
            },
            fallback_typefaces: {
                "0": contains {
                    family_name: "Alpha",
                    style: contains {
                        width: "normal",
                    },
                },
                "1": contains {
                    family_name: "Alpha",
                    style: contains {
                        width: "condensed",
                    },
                },
                "2": contains {
                    family_name: "Beta",
                    style: contains {
                        width: "normal",
                    },
                }
            }
        });

        Ok(())
    }
}

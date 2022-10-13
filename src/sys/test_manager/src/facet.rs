// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{HERMETIC_TESTS_COLLECTION, TEST_TYPE_REALM_MAP},
        error::{FacetError, LaunchTestError},
    },
    anyhow::format_err,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_resolution as fresolution,
    fidl_fuchsia_data as fdata,
    std::sync::Arc,
};

const TEST_TYPE_FACET_KEY: &'static str = "fuchsia.test.type";
pub const TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY: &'static str =
    "fuchsia.test.deprecated-allowed-packages";
const TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY: &'static str =
    "fuchsia.test.deprecated-allowed-all-packages";

/// Set of facets attached to a test component's manifest that describe how to run it.
#[derive(Debug)]
pub(crate) struct SuiteFacets {
    pub collection: &'static str,
    pub deprecated_allowed_packages: Option<Vec<String>>,
    pub deprecated_allowed_all_packages: Option<bool>,
}

pub(crate) enum ResolveStatus {
    Unresolved,
    Resolved(Result<SuiteFacets, LaunchTestError>),
}

pub(crate) async fn get_suite_facets(
    test_url: String,
    resolver: Arc<fresolution::ResolverProxy>,
) -> Result<SuiteFacets, LaunchTestError> {
    let component = resolver
        .resolve(&test_url)
        .await
        .map_err(|e| LaunchTestError::ResolveTest(e.into()))?
        .map_err(|e| LaunchTestError::ResolveTest(format_err!("{:?}", e)))?;
    let decl = component.decl.unwrap();
    let bytes = mem_util::bytes_from_data(&decl).map_err(LaunchTestError::ManifestIo)?;
    let component_decl: fdecl::Component = fidl::encoding::decode_persistent(&bytes)
        .map_err(|e| LaunchTestError::InvalidManifest(e.into()))?;

    parse_facet(&component_decl).map_err(|e| e.into())
}

fn parse_facet(decl: &fdecl::Component) -> Result<SuiteFacets, FacetError> {
    let mut collection = HERMETIC_TESTS_COLLECTION;
    let mut deprecated_allowed_all_packages = None;
    let mut deprecated_allowed_packages = None;
    if let Some(obj) = &decl.facets {
        let entries = obj.entries.as_ref().map(Vec::as_slice).unwrap_or_default();
        for entry in entries {
            if entry.key == TEST_TYPE_FACET_KEY {
                collection = parse_suite_collection(&entry)?;
            } else if entry.key == TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY {
                let test_type = entry
                    .value
                    .as_ref()
                    .ok_or(FacetError::NullFacet(TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY))?;
                // Temporarily allow unreachable patterns while fuchsia.data.DictionaryValue
                // is migrated from `strict` to `flexible`.
                // TODO(https://fxbug.dev/92247): Remove this.
                #[allow(unreachable_patterns)]
                match test_type.as_ref() {
                    fdata::DictionaryValue::Str(s) => match s.trim().parse::<bool>() {
                        Ok(s) => deprecated_allowed_all_packages = Some(s),
                        Err(_) => {
                            return Err(FacetError::InvalidFacetValue(
                                TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY,
                                format!("{:?}", test_type),
                                "'true, false'".to_string(),
                            ));
                        }
                    },
                    _ => {
                        return Err(FacetError::InvalidFacetValue(
                            TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY,
                            format!("{:?}", test_type),
                            "'true, false'".to_string(),
                        ));
                    }
                }
            } else if entry.key == TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY {
                let test_type = entry
                    .value
                    .as_ref()
                    .ok_or(FacetError::NullFacet(TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY))?;
                // Temporarily allow unreachable patterns while fuchsia.data.DictionaryValue
                // is migrated from `strict` to `flexible`.
                // TODO(https://fxbug.dev/92247): Remove this.
                #[allow(unreachable_patterns)]
                match test_type.as_ref() {
                    fdata::DictionaryValue::StrVec(s) => {
                        deprecated_allowed_packages = Some(s.clone());
                    }
                    _ => {
                        return Err(FacetError::InvalidFacetValue(
                            TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY,
                            format!("{:?}", test_type),
                            "vector of allowed packages names".to_string(),
                        ));
                    }
                }
            }
        }
    }
    Ok(SuiteFacets { collection, deprecated_allowed_packages, deprecated_allowed_all_packages })
}

fn parse_suite_collection(entry: &fdata::DictionaryEntry) -> Result<&'static str, FacetError> {
    let test_type = entry.value.as_ref().ok_or(FacetError::NullFacet(TEST_TYPE_FACET_KEY))?;
    // Temporarily allow unreachable patterns while fuchsia.data.DictionaryValue
    // is migrated from `strict` to `flexible`.
    // TODO(https://fxbug.dev/92247): Remove this.
    #[allow(unreachable_patterns)]
    match test_type.as_ref() {
        fdata::DictionaryValue::Str(s) => {
            if TEST_TYPE_REALM_MAP.contains_key(s.as_str()) {
                return Ok(TEST_TYPE_REALM_MAP[s.as_str()]);
            }
            return Err(FacetError::InvalidFacetValue(
                TEST_TYPE_FACET_KEY,
                format!("{:?}", s),
                format!(
                    "one of {}",
                    TEST_TYPE_REALM_MAP
                        .keys()
                        .map(|k| k.to_string())
                        .collect::<Vec<_>>()
                        .join(", ")
                ),
            ));
        }
        fdata::DictionaryValue::StrVec(s) => {
            return Err(FacetError::InvalidFacetValue(
                TEST_TYPE_FACET_KEY,
                format!("{:?}", s),
                format!(
                    "one of {}",
                    TEST_TYPE_REALM_MAP
                        .keys()
                        .map(|k| k.to_string())
                        .collect::<Vec<_>>()
                        .join(", ")
                ),
            ));
        }
        _ => {
            return Err(FacetError::InvalidFacetValue(
                TEST_TYPE_FACET_KEY,
                format!("{:?}", test_type),
                format!(
                    "one of {}",
                    TEST_TYPE_REALM_MAP
                        .keys()
                        .map(|k| k.to_string())
                        .collect::<Vec<_>>()
                        .join(", ")
                ),
            ));
        }
    };
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::constants::{
        CHROMIUM_TESTS_COLLECTION, CTS_TESTS_COLLECTION, SYSTEM_TESTS_COLLECTION,
        VULKAN_TESTS_COLLECTION,
    };

    #[test]
    fn parse_suite_collection_works() {
        const TEST_FACET: &str = "fuchsia.test";

        // test that default hermetic value is true
        let mut decl = fdecl::Component::EMPTY;
        assert_eq!(parse_facet(&decl).unwrap().collection, HERMETIC_TESTS_COLLECTION);

        // empty facet
        decl.facets =
            Some(fdata::Dictionary { entries: vec![].into(), ..fdata::Dictionary::EMPTY });
        assert_eq!(parse_facet(&decl).unwrap().collection, HERMETIC_TESTS_COLLECTION);

        // empty facet
        decl.facets = Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY });
        assert_eq!(parse_facet(&decl).unwrap().collection, HERMETIC_TESTS_COLLECTION);

        // make sure that the func can handle some other facet key
        decl.facets = Some(fdata::Dictionary {
            entries: vec![fdata::DictionaryEntry { key: "somekey".into(), value: None }].into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, HERMETIC_TESTS_COLLECTION);

        // test facet with some other key works
        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, HERMETIC_TESTS_COLLECTION);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_TYPE_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("hermetic".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, HERMETIC_TESTS_COLLECTION);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_TYPE_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("system".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, SYSTEM_TESTS_COLLECTION);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_TYPE_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("cts".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, CTS_TESTS_COLLECTION);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_TYPE_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("vulkan".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, VULKAN_TESTS_COLLECTION);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_TYPE_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("chromium".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(parse_facet(&decl).unwrap().collection, CHROMIUM_TESTS_COLLECTION);

        // invalid facets
        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_TYPE_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("some_other_collection".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let _ = parse_facet(&decl).expect_err("this should have failed");

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry { key: TEST_TYPE_FACET_KEY.into(), value: None },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let _ = parse_facet(&decl).expect_err("this should have failed");
    }

    #[test]
    fn parse_allowed_packages_works() {
        const TEST_FACET: &str = "fuchsia.test";

        let mut decl = fdecl::Component::EMPTY;
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(facet.deprecated_allowed_packages, None);

        // empty facet
        decl.facets =
            Some(fdata::Dictionary { entries: vec![].into(), ..fdata::Dictionary::EMPTY });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(facet.deprecated_allowed_packages, None);

        // empty facet
        decl.facets = Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(facet.deprecated_allowed_packages, None);

        // make sure that the func can handle some other facet key
        decl.facets = Some(fdata::Dictionary {
            entries: vec![fdata::DictionaryEntry { key: "somekey".into(), value: None }].into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(facet.deprecated_allowed_packages, None);

        // test facet with some other key works
        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(facet.deprecated_allowed_packages, None);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("true".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, Some(true));
        assert_eq!(facet.deprecated_allowed_packages, None);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("false".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, Some(false));
        assert_eq!(facet.deprecated_allowed_packages, None);

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY.into(),
                    value: Some(
                        fdata::DictionaryValue::StrVec(vec![
                            "package-one".into(),
                            "package-two".into(),
                        ])
                        .into(),
                    ),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(
            facet.deprecated_allowed_packages,
            Some(vec!["package-one".into(), "package-two".into()])
        );

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::StrVec(vec![]).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, None);
        assert_eq!(facet.deprecated_allowed_packages, Some(vec![]));

        decl.facets = Some(fdata::Dictionary {
            entries: vec![
                fdata::DictionaryEntry { key: "somekey".into(), value: None },
                fdata::DictionaryEntry {
                    key: format!("{}.somekey", TEST_FACET),
                    value: Some(fdata::DictionaryValue::Str("some_string".into()).into()),
                },
                fdata::DictionaryEntry {
                    key: TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY.into(),
                    value: Some(
                        fdata::DictionaryValue::StrVec(vec![
                            "package-one".into(),
                            "package-two".into(),
                        ])
                        .into(),
                    ),
                },
                fdata::DictionaryEntry {
                    key: TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY.into(),
                    value: Some(fdata::DictionaryValue::Str("true".into()).into()),
                },
            ]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let facet = parse_facet(&decl).unwrap();
        assert_eq!(facet.deprecated_allowed_all_packages, Some(true));
        assert_eq!(
            facet.deprecated_allowed_packages,
            Some(vec!["package-one".into(), "package-two".into()])
        );

        decl.facets = Some(fdata::Dictionary {
            entries: vec![fdata::DictionaryEntry {
                key: TEST_DEPRECATED_ALLOWED_ALL_PACKAGES_FACET_KEY.into(),
                value: Some(fdata::DictionaryValue::Str("something".into()).into()),
            }]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let _ = parse_facet(&decl).expect_err("this should have failed");

        decl.facets = Some(fdata::Dictionary {
            entries: vec![fdata::DictionaryEntry {
                key: TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY.into(),
                value: Some(fdata::DictionaryValue::Str("something".into()).into()),
            }]
            .into(),
            ..fdata::Dictionary::EMPTY
        });
        let _ = parse_facet(&decl).expect_err("this should have failed");
    }
}

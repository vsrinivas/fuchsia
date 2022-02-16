// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::{FacetError, LaunchTestError},
        HERMETIC_TESTS_COLLECTION, TEST_TYPE_REALM_MAP,
    },
    anyhow::format_err,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
};

const TEST_TYPE_FACET_KEY: &'static str = "fuchsia.test.type";

/// Set of facets attached to a test component's manifest that describe how to run it.
pub(crate) struct SuiteFacets {
    pub collection: &'static str,
}

pub(crate) async fn get_suite_facets(
    test_url: &str,
    resolver: &fsys::ComponentResolverProxy,
) -> Result<SuiteFacets, LaunchTestError> {
    let component = resolver
        .resolve(test_url)
        .await
        .map_err(|e| LaunchTestError::ResolveTest(e.into()))?
        .map_err(|e| LaunchTestError::ResolveTest(format_err!("{:?}", e)))?;
    let decl = component.decl.unwrap();
    let bytes = mem_util::bytes_from_data(&decl).map_err(LaunchTestError::ManifestIo)?;
    let component_decl: fdecl::Component = fidl::encoding::decode_persistent(&bytes)
        .map_err(|e| LaunchTestError::InvalidManifest(e.into()))?;

    let collection = get_suite_collection(&component_decl)?;
    Ok(SuiteFacets { collection })
}

fn get_suite_collection(decl: &fdecl::Component) -> Result<&'static str, FacetError> {
    if let Some(obj) = &decl.facets {
        let entries = obj.entries.as_ref().map(Vec::as_slice).unwrap_or_default();
        for entry in entries {
            if entry.key == TEST_TYPE_FACET_KEY {
                let test_type =
                    entry.value.as_ref().ok_or(FacetError::NullFacet(TEST_TYPE_FACET_KEY))?;
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
        }
    }
    Ok(HERMETIC_TESTS_COLLECTION)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{
        CHROMIUM_TESTS_COLLECTION, CTS_TESTS_COLLECTION, SYSTEM_TESTS_COLLECTION,
        VULKAN_TESTS_COLLECTION,
    };

    #[test]
    fn get_suite_collection_works() {
        const TEST_FACET: &str = "fuchsia.test";

        // test that default hermetic value is true
        let mut decl = fdecl::Component::EMPTY;
        assert_eq!(get_suite_collection(&decl).unwrap(), HERMETIC_TESTS_COLLECTION);

        // empty facet
        decl.facets =
            Some(fdata::Dictionary { entries: vec![].into(), ..fdata::Dictionary::EMPTY });
        assert_eq!(get_suite_collection(&decl).unwrap(), HERMETIC_TESTS_COLLECTION);

        // empty facet
        decl.facets = Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY });
        assert_eq!(get_suite_collection(&decl).unwrap(), HERMETIC_TESTS_COLLECTION);

        // make sure that the func can handle some other facet key
        decl.facets = Some(fdata::Dictionary {
            entries: vec![fdata::DictionaryEntry { key: "somekey".into(), value: None }].into(),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(get_suite_collection(&decl).unwrap(), HERMETIC_TESTS_COLLECTION);

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
        assert_eq!(get_suite_collection(&decl).unwrap(), HERMETIC_TESTS_COLLECTION);

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
        assert_eq!(get_suite_collection(&decl).unwrap(), HERMETIC_TESTS_COLLECTION);

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
        assert_eq!(get_suite_collection(&decl).unwrap(), SYSTEM_TESTS_COLLECTION);

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
        assert_eq!(get_suite_collection(&decl).unwrap(), CTS_TESTS_COLLECTION);

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
        assert_eq!(get_suite_collection(&decl).unwrap(), VULKAN_TESTS_COLLECTION);

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
        assert_eq!(get_suite_collection(&decl).unwrap(), CHROMIUM_TESTS_COLLECTION);

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
        let _ = get_suite_collection(&decl).expect_err("this should have failed");

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
        let _ = get_suite_collection(&decl).expect_err("this should have failed");
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::PackagePath,
    fuchsia_url::test::{random_package_name, random_package_variant},
    proptest::prelude::*,
};

#[cfg(test)]
use {
    crate::{CreationManifest, MetaPackage},
    fuchsia_url::test::random_resource_path,
};

#[cfg(test)]
prop_compose! {
    /// Returns a random valid host path character. Host platforms have different requirements on
    /// what are valid path characters. On linux, it can be any character except for '/' and null,
    /// whereas APFS also requires that any unicode codepoints must be defined in Unicode 9.0.
    /// Furthermore, some filesystems are case insensitive, and so a file named "foo" and "Foo"
    /// conflict.
    ///
    /// For our testing purposes, we don't really care about testing what paths an operating system
    /// supports, so we'll just use randomly generate lowercase alphanumeric characters.
    fn random_host_path_char()(c in "[a-z0-9]") -> String {
        c
    }
}

#[cfg(test)]
prop_compose! {
    fn random_host_path_component
        (min: usize, max: usize)
        (s in prop::collection::vec(random_host_path_char(), min..max)) -> String {
            s.join("")
        }
}

#[cfg(test)]
prop_compose! {
    fn random_host_path
        (min: usize, max: usize)
        (s in prop::collection::vec(random_host_path_component(1, 4), min..max))
         -> String
    {
        s.join("/")
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_external_resource_path()
        (s in random_resource_path(1, 4)
         .prop_filter(
             "External package contents cannot be in the 'meta/' directory",
             |s| !s.starts_with("meta/"))
        ) -> String
    {
        s
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_far_resource_path()
        (s in random_resource_path(1, 4)) -> String
    {
        format!("meta/{}", s)
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_hash()(s: [u8; 32]) -> fuchsia_merkle::Hash {
        s.into()
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_merkle_hex()(s in "[[:xdigit:]]{64}") -> String {
        s
    }
}

#[cfg(test)]
prop_compose! {
    fn random_creation_manifest_result()
        (external_content in prop::collection::btree_map(
            random_external_resource_path(), random_host_path(1, 2), 1..4),
         mut far_content in prop::collection::btree_map(
             random_far_resource_path(), random_host_path(1, 2), 1..4),)
         -> Result<CreationManifest, crate::errors::CreationManifestError>
    {
        far_content.insert("meta/package".to_string(), "meta/package".to_string());
        CreationManifest::from_external_and_far_contents(
            external_content, far_content)
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_creation_manifest()
        (manifest_result in random_creation_manifest_result().prop_filter(
            "path combinations cannot have file/directory collisions, like ['foo', 'foo/bar']",
            |r| r.is_ok()
        ))
         -> CreationManifest
    {
        manifest_result.unwrap()
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_meta_package()
        (name in random_package_name(),
        variant in random_package_variant())
         -> MetaPackage
    {
        MetaPackage::from_name_and_variant(name, variant)
    }
}

prop_compose! {
    pub fn random_package_path()(
        name in random_package_name(),
        variant in random_package_variant()
    ) -> PackagePath {
        PackagePath::from_name_and_variant(name, variant)
    }
}

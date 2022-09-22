// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use std::collections::HashMap;

pub const TEST_ROOT_REALM_NAME: &'static str = "test_root";

// TODO(fxbug.dev/100034): Delete these once we no longer need to hard code these in the code.
pub const HERMETIC_ENVIRONMENT_NAME: &'static str = "hermetic";
pub const HERMETIC_TESTS_COLLECTION: &'static str = "tests";
pub const HERMETIC_TIER_2_TESTS_COLLECTION: &'static str = "tier-2-tests";
pub const STARNIX_TESTS_COLLECTION: &'static str = "starnix-tests";
pub const SYSTEM_TESTS_COLLECTION: &'static str = "system-tests";
pub const CTS_TESTS_COLLECTION: &'static str = "cts-tests";
pub const VULKAN_TESTS_COLLECTION: &'static str = "vulkan-tests";
pub const CHROMIUM_TESTS_COLLECTION: &'static str = "chromium-tests";
pub const DRM_TESTS_COLLECTION: &'static str = "drm-tests";
pub const MEDIA_TESTS_COLLECTION: &'static str = "media-tests";
pub const GOOGLE_TESTS_COLLECTION: &'static str = "google-tests";
pub const DEVICES_TESTS_COLLECTION: &'static str = "devices-tests";
pub const VFS_COMPLIANCE_COLLECTION: &'static str = "vfs-compliance-tests";

lazy_static! {
    pub static ref TEST_TYPE_REALM_MAP: HashMap<&'static str, &'static str> = [
        ("hermetic", HERMETIC_TESTS_COLLECTION),
        ("hermetic-tier-2", HERMETIC_TIER_2_TESTS_COLLECTION),
        ("system", SYSTEM_TESTS_COLLECTION),
        ("cts", CTS_TESTS_COLLECTION),
        ("vulkan", VULKAN_TESTS_COLLECTION),
        ("chromium", CHROMIUM_TESTS_COLLECTION),
        ("devices", DEVICES_TESTS_COLLECTION),
        ("drm", DRM_TESTS_COLLECTION),
        ("media", MEDIA_TESTS_COLLECTION),
        ("google", GOOGLE_TESTS_COLLECTION),
        ("starnix", STARNIX_TESTS_COLLECTION),
        ("vfs-compliance", VFS_COMPLIANCE_COLLECTION),
    ]
    .iter()
    .copied()
    .collect();
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod experimental_api;
mod old_api;
mod reviewed_api;
pub mod util;

pub const FONTS_EPHEMERAL_CM: &str =
    "fuchsia-pkg://fuchsia.com/font_provider_integration_tests#meta/fonts_with_ephemeral_fonts.cm";
pub const FONTS_ALIASED_CM: &str =
    "fuchsia-pkg://fuchsia.com/font_provider_integration_tests#meta/fonts_with_aliases_fonts.cm";
pub const FONTS_SMALL_CM: &str =
    "fuchsia-pkg://fuchsia.com/font_provider_integration_tests#meta/fonts_with_small_fonts.cm";
pub const FONTS_MEDIUM_CM: &str =
    "fuchsia-pkg://fuchsia.com/font_provider_integration_tests#meta/fonts_with_medium_fonts.cm";
pub const FONTS_LARGE_CM: &str =
    "fuchsia-pkg://fuchsia.com/font_provider_integration_tests#meta/fonts_with_large_fonts.cm";

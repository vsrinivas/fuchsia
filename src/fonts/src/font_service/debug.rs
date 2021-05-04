// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_fonts::{self as fonts},
    fuchsia_syslog::*,
    std::{fs, stringify},
};

macro_rules! format_field {
    ($debug_struct:expr, $parent:expr, $field:ident, $wrapper:path) => {
        if let Some(f) = &$parent.0.$field {
            $debug_struct.field(std::stringify!($field), &$wrapper(f));
        }
    };
    ($debug_struct:expr, $parent:expr, $field:ident) => {
        if let Some(f) = &$parent.0.$field {
            $debug_struct.field(std::stringify!($field), f);
        }
    };
}

const BUILD_TYPE_PATH: &str = "/config/data/build/type";

/// Tries to read the build type from `/config/data`. Returns true if the build
/// is "eng" or "userdebug", false if "user". Also returns false if the build type could not be
/// read.
pub fn is_internal_build() -> bool {
    BuildType::load_from_config_data().map_or_else(
        |e| {
            fx_log_warn!("Failed to read Fuchsia build type: {:?}", e);
            // Fail closed, i.e. assume a production build unless we know otherwise.
            false
        },
        |build_type| {
            fx_log_debug!("Build type: {}", build_type);
            build_type.is_internal_build()
        },
    )
}

/// Fuchsia build flavors
pub enum BuildType {
    User,
    UserDebug,
    Eng,
}

impl BuildType {
    /// Loads the current Fuchsia environment's build type from a file in `/config/data`.
    ///
    /// The file must be supplied using a [`build_type_config_data` GN target][config-gni].
    ///
    /// [config-gni]: https://cs.opensource.google/fuchsia/fuchsia/+/main:build/type/config.gni
    pub fn load_from_config_data() -> Result<BuildType, Error> {
        fs::read_to_string(BUILD_TYPE_PATH)?.trim().parse()
    }

    /// Returns true if this is an internal Fuchsia build.
    pub fn is_internal_build(&self) -> bool {
        match self {
            BuildType::UserDebug | BuildType::Eng => true,
            BuildType::User => false,
        }
    }
}

impl std::fmt::Display for BuildType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            BuildType::User => "user",
            BuildType::UserDebug => "userdebug",
            BuildType::Eng => "eng",
        };
        write!(f, "{}", s)
    }
}

/// Same strings as `Display`.
impl std::fmt::Debug for BuildType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(&self, f)
    }
}

impl std::str::FromStr for BuildType {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "user" => Ok(BuildType::User),
            "userdebug" => Ok(BuildType::UserDebug),
            "eng" => Ok(BuildType::Eng),
            _ => Err(format_err!("Unknown build type: {}", s)),
        }
    }
}

/// Formats a [`fidl_fuchsia_fonts::TypefaceRequest`], skipping empty fields.
pub struct TypefaceRequestFormatter<'a>(pub &'a fonts::TypefaceRequest);

impl<'a> std::fmt::Debug for TypefaceRequestFormatter<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut d = f.debug_struct(stringify!(TypefaceRequest));

        format_field!(d, self, query, TypefaceQueryFormatter);
        format_field!(d, self, flags);
        format_field!(d, self, cache_miss_policy);

        d.finish()
    }
}

/// Formats a [`fidl_fuchsia_fonts::TypefaceQuery`], skipping empty fields.
pub struct TypefaceQueryFormatter<'a>(pub &'a fonts::TypefaceQuery);

impl<'a> std::fmt::Debug for TypefaceQueryFormatter<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut d = f.debug_struct(stringify!(TypefaceQuery));

        format_field!(d, self, family);
        format_field!(d, self, style, Style2Formatter);
        format_field!(d, self, languages);
        format_field!(d, self, code_points);
        format_field!(d, self, fallback_family);

        d.finish()
    }
}

/// Formats a [`fidl_fuchsia_fonts::Style2`], skipping empty fields.
pub struct Style2Formatter<'a>(pub &'a fonts::Style2);

impl<'a> std::fmt::Debug for Style2Formatter<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut d = f.debug_struct(stringify!(Style2));

        format_field!(d, self, slant);
        format_field!(d, self, weight);
        format_field!(d, self, width);

        d.finish()
    }
}

pub struct TypefaceResponseFormatter<'a>(pub &'a fonts::TypefaceResponse);

impl<'a> std::fmt::Debug for TypefaceResponseFormatter<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut d = f.debug_struct(stringify!(TypefaceResponse));

        format_field!(d, self, buffer);
        format_field!(d, self, buffer_id);
        format_field!(d, self, font_index);

        d.finish()
    }
}

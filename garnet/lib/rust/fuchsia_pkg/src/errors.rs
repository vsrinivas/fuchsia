// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;

#[derive(Clone, Debug, PartialEq, Eq, Fail)]
pub enum PackagePathError {
    #[fail(display = "object names must be at least 1 byte")]
    NameEmpty,

    #[fail(display = "object names must be at most 255 bytes")]
    NameTooLong,

    #[fail(display = "object names cannot contain the NULL byte")]
    NameContainsNull,

    #[fail(display = "object names cannot be '.'")]
    NameIsDot,

    #[fail(display = "object names cannot be '..'")]
    NameIsDotDot,

    #[fail(display = "object paths cannot start with '/'")]
    PathStartsWithSlash,

    #[fail(display = "object paths cannot end with '/'")]
    PathEndsWithSlash,

    #[fail(display = "object paths must be at least 1 byte")]
    PathIsEmpty,
}

#[derive(Debug, Fail)]
pub enum CreationManifestError {
    #[fail(display = "manifest contains an invalid package path '{}'. {}", path, cause)]
    PackagePath {
        #[cause]
        cause: PackagePathError,
        path: String,
    },

    #[fail(display = "attempted to deserialize creation manifest from malformed json: {}", _0)]
    Json(#[cause] serde_json::Error),

    #[fail(display = "package external content cannot be in 'meta/' directory: {}", path)]
    ExternalContentInMetaDirectory { path: String },
}

impl From<serde_json::Error> for CreationManifestError {
    fn from(err: serde_json::Error) -> Self {
        CreationManifestError::Json(err)
    }
}

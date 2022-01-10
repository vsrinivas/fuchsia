// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    serde_json::value::Value,
    std::collections::HashMap,
};

/// JSON structure of the content returned by hitting the package server for targets.json
/// ie: http://0.0.0.0:8083/targets.json
///
/// Path: /targets.json
#[derive(Deserialize)]
pub struct TargetsJson {
    pub signed: Signed,
}

/// JSON structure of the `signed` field in targets.json.
/// Contains information about target packages.
///
/// Path: /targets.json -> signed
#[derive(Deserialize)]
pub struct Signed {
    pub targets: HashMap<String, FarPackageDefinition>,
}

/// JSON structure containing information about the FAR packages provided by the package server.
///
/// Path: /targets.json -> signed -> targets<>
#[derive(Deserialize)]
pub struct FarPackageDefinition {
    pub custom: Custom,
}

/// JSON structure of the custom field in a FAR package definition.
///
/// Path: /targets.json -> signed -> targets<> -> custom
#[derive(Deserialize)]
pub struct Custom {
    pub merkle: String,
}

/// JSON structure of a blob retrieved from package server via merkle hash that
/// defines a service package.
/// There is no guarantee that all blobs vended from the package server follow this
/// format. A caller must correctly identify the expected blob format when making
/// a request to /blobs/{merkle}
///
/// Path: /blobs/{merkle}
#[derive(Deserialize)]
pub struct ServicePackageDefinition {
    /// Map of a service name to a provider (ie. component) of that service.
    pub services: Option<HashMap<String, Value>>,
    /// List of component Urls started on sys realm launch
    pub apps: Option<Vec<String>>,
}

/// JSON structure that contains the sandbox definition defined for builtin packages.
///
/// Path: //builtins.json -> packages[] -> manifest
#[derive(Deserialize)]
pub struct Manifest {
    pub sandbox: Sandbox,
}

/// JSON structure of a cmx file contained in a FAR archive, read via a FAR reader.
///
/// Path: //far_file
#[derive(Deserialize, Serialize)]
pub struct CmxJson {
    pub sandbox: Option<Sandbox>,
}

/// JSON structure of the sandbox defined by the cmx file defining a component.
///
/// Path: //far_file -> sandbox
#[derive(Deserialize, Serialize)]
pub struct Sandbox {
    pub dev: Option<Vec<String>>,
    pub services: Option<Vec<String>>,
    pub system: Option<Vec<String>>,
    pub pkgfs: Option<Vec<String>>,
    pub features: Option<Vec<String>>,
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::types::*,
    serde::{Deserialize, Serialize},
    serde_json::value::Value,
    std::collections::HashMap,
};

/// This file contains types that encapsulate JSON objects to be parsed by serde_json.
/// Currently unused fields have been commented out, but should be able to be uncommented
/// as they come into use.

/* targets.json */

/// JSON structure of the content returned by hitting the package server for targets.json
/// ie: http://0.0.0.0:8083/targets.json
///
/// Path: /targets.json
#[derive(Deserialize)]
pub struct TargetsJson {
    //pub signatures: Vec<Signatures>,
    pub signed: Signed,
}

/// JSON structure of the `signatures` field in targets.json.
/// Contains the signature for verification.
///
/// Path: /targets.json -> signatures[]
// #[derive(Deserialize)]
// pub struct Signatures {
//     pub keyid: String,
//     pub method: String,
//     pub sig: String,
// }

/// JSON structure of the `signed` field in targets.json.
/// Contains information about target packages.
///
/// Path: /targets.json -> signed
#[derive(Deserialize)]
pub struct Signed {
    //pub typ: String,
    //pub expires: String,
    //pub spec_version: String,
    pub targets: HashMap<String, FarPackageDefinition>,
    //pub version: usize,
}

/// JSON structure containing information about the FAR packages provided by the package server.
///
/// Path: /targets.json -> signed -> targets<>
#[derive(Deserialize)]
pub struct FarPackageDefinition {
    pub custom: Custom,
    //pub hashes: HashMap<String, String>,
    //pub length: usize,
}

/// JSON structure of the custom field in a FAR package definition.
///
/// Path: /targets.json -> signed -> targets<> -> custom
#[derive(Deserialize)]
pub struct Custom {
    pub merkle: String,
    //pub size: usize,
}

/* service package definition retrieved via package server */

/// JSON structure of a blob retrieved from package server via merkle hash that
/// defines a service package.
/// There is no guarantee that all blobs vended from the package server follow this
/// format. A caller must correctly identify the expected blob format when making
/// a request to /blobs/{merkle}
///
/// Path: /blobs/{merkle}
#[derive(Deserialize)]
pub struct ServicePackageDefinition {
    pub services: Option<HashMap<String, Value>>,
    // FIXME: Other json structs are built such that unused fields are present but commented out.
    // This one omits the completely.
}

/* builtins.json */

/// JSON structure of the manually configured builtins.json file used in the initial
/// version of component-graph in order to augment service mappings.
///
/// Path: <local_path_to_builtins>/builtins.json
#[derive(Deserialize)]
pub struct BuiltinsJson {
    pub packages: Vec<BuiltinPackageDefinition>,
    pub services: ServiceMapping,
}

/// JSON structure of the builtin packages included in builtins.json.
/// Aims to define a similar structure as the cmx files read in via the
/// FAR reader for packages from the package server.
///
/// Path: //builtins.json -> packages[]
#[derive(Deserialize)]
pub struct BuiltinPackageDefinition {
    pub url: String,
    pub manifest: Option<Manifest>,
}

/// JSON structure that contains the sandbox definition defined for builtin packages.
///
/// Path: //builtins.json -> packages[] -> manifest
#[derive(Deserialize)]
pub struct Manifest {
    pub sandbox: Sandbox,
}

/* cmx file format read via far reader */

/// JSON structure of a cmx file contained in a FAR archive, read via a FAR reader.
///
/// Path: //far_file
#[derive(Deserialize, Serialize)]
pub struct CmxJson {
    //pub program: Option<Program>,
    pub sandbox: Option<Sandbox>,
    //pub runner: Option<String>,
}

/// JSON structure of the program block in a cmx file.
/// In the case where //far_file -> runner is not present, this will be a string pointing to
/// the binary to run the program.
/// In the case where //far_file -> runner is present, this will be a string-string map
/// interpreted as args to be passed to the runner.
/// https://fuchsia.googlesource.com/docs/+/7291604af051af6c62259df2ceca2a6f3d958ba1/the-book/package_metadata.md#program
///
/// Path: //far_file -> program
// #[derive(Deserialize)]
// pub struct Program {
//     pub binary: Option<String>,
// }

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

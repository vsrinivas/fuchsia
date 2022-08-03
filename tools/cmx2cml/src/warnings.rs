// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{BUILD_INFO_PROTOCOL, ELF_TEST_RUNNER_SHARD};

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum Warning {
    /// Users must declare any capabilities that are provided by the component
    DeclareExpose,

    /// Users might want to use gtest/gunit/rust/gotest but we don't yet support them
    ElfTestRunnerUsed,

    /// CMX had includes that we naively converted to .cml
    IncludesRenamed,

    /// Component retrieved build info through directory API and must switch to protocol.
    BuildInfoImpl,

    /// Component asks for hub feature
    UsesHub,

    /// Child needs an additional GN target added to package, possibly capabilities routed to it
    ChildNeedsGnTargetAndRouting { child: String, gn_target: String },

    /// Component must be added to the storage index
    StorageIndex,

    /// Test will require a mock for config-data
    ConfigDataInTest,

    /// Component uses device directories but we can't translate them perfectly
    DeviceDirectoryBestEffort,

    /// Test asks for a protocol that's unavailable to hermetic & system tests
    TestWithUnavailableProtocol(String),
}

const EXPOSE_WARNING: &str = r#"
// WARNING: Components must declare capabilities they provide to parents.
//          Either delete or uncomment and populate these lines:
//
// capabilities: [
//     {
//          protocol: [ "fuchsia.example.Protocol" ],
//     },
// ],
// expose: [
//     {
//          protocol: [ "fuchsia.example.Protocol" ],
//          from: "self",
//     },
// ],"#;

const ELF_TEST_RUNNER_WARNING: &str = r#"
// NOTE: You may want to choose a test runner that understands your language's tests. See
// https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework?hl=en#inventory_of_test_runners
// for details.
"#;

const INCLUDE_RENAME_WARNING: &str = r#"
// WARNING: These includes have been mechanically renamed from .cmx to .cml, it's possible
// that some of them do not yet have CML equivalents. Check with authors of the v1 shards
// if you get build errors using this manifest."#;

const BUILD_INFO_WARNING: &str = r#"
// WARNING: Build info is delivered differently in v1 & v2. See
// https://fuchsia.dev/fuchsia-src/development/components/v2/migration/features#build-info."#;

const HUB_WARNING: &str = r#"
// WARNING: Event streams replace the hub for testing in v2. For more information:
// https://fuchsia.dev/fuchsia-src/development/components/v2/migration/features#events"#;

const STORAGE_INDEX_WARNING: &str = r#"
// NOTE: Using persistent storage requires updating the storage index. For more details:
// https://fuchsia.dev/fuchsia-src/development/components/v2/migration/features#update_component_storage_index"#;

const CONFIG_DATA_TEST_WARNING: &str = r#"
// NOTE: config-data in tests requires specifying the package:
// https://fuchsia.dev/fuchsia-src/development/components/v2/migration/features?hl=en#configuration_data_in_tests
"#;

const DEVICE_DIRECTORY_WARNING: &str = r#"
// WARNING: Device directories are converted as best-effort and may need either different rights or
// a different directory name to function in v2."#;

const UNAVAILABLE_TEST_PROTOCOL_WARNING: &str = r#"
// WARNING: This protocol is not normally available to tests, you may need to add it to the
// system test realm or add a mock/fake implementation as a child.
"#;

impl Warning {
    pub fn apply(&self, lines: &mut Vec<String>) {
        match self {
            Warning::DeclareExpose => {
                let use_or_end_idx =
                    lines.iter().position(|l| l == "    use: [").unwrap_or_else(|| {
                        lines.iter().position(|l| l == "}").expect(
                            "generated manifests all have a closing brace on their own line",
                        )
                    });
                lines.insert(use_or_end_idx, EXPOSE_WARNING.to_string());
            }
            Warning::ElfTestRunnerUsed => {
                let runner_shard_idx = lines
                    .iter()
                    .position(|l| l.contains(ELF_TEST_RUNNER_SHARD))
                    .expect("files with the elf test runner warning must include the shard");
                lines.insert(runner_shard_idx, ELF_TEST_RUNNER_WARNING.to_string());
            }
            Warning::IncludesRenamed => {
                let includes_idx = lines
                    .iter()
                    .position(|l| l.starts_with("    include: ["))
                    .expect("files with include conversion warnings must have an include block");
                lines.insert(includes_idx, INCLUDE_RENAME_WARNING.to_string());
            }
            Warning::BuildInfoImpl => {
                let build_info_proto_idx = lines
                    .iter()
                    .position(|l| l.contains(BUILD_INFO_PROTOCOL))
                    .expect("files with build info warning list the build info protocol");
                lines.insert(build_info_proto_idx, BUILD_INFO_WARNING.to_string());
            }
            Warning::UsesHub => {
                let opening_brace_idx = lines
                    .iter()
                    .position(|l| l == "{")
                    .expect("all files have an opening brace on its own line");
                lines.insert(opening_brace_idx, HUB_WARNING.to_string());
            }
            Warning::StorageIndex => {
                let storage_idx = lines
                    .iter()
                    .position(|l| l.contains("storage: \"data\","))
                    .expect("files with storage index warnings have a persistent data directory");
                lines.insert(storage_idx, STORAGE_INDEX_WARNING.to_string());
            }
            Warning::ChildNeedsGnTargetAndRouting { child, gn_target } => {
                let child_name_idx = lines
                    .iter()
                    .position(|l| l.contains("name: ") && l.contains(&*child))
                    .expect("child warnings are only emitted for children we have");
                lines.insert(
                    child_name_idx,
                    format!(
                        r#"
// WARNING: This child must be packaged with your component. The package should depend on:
//     {}
// Note that you may need to route additional capabilities to this child."#,
                        gn_target
                    ),
                );
            }
            Warning::ConfigDataInTest => {
                let config_data_idx = lines
                    .iter()
                    .position(|l| l.contains("config-data"))
                    .expect("config-data warnings are only emitted for cml's with the use");
                lines.insert(config_data_idx, CONFIG_DATA_TEST_WARNING.to_string());
            }
            Warning::DeviceDirectoryBestEffort => {
                let dev_idx = lines
                    .iter()
                    .position(|l| l.contains("directory: \"dev-"))
                    .expect("device dir warnings are only emitted for cml's with devices");
                lines.insert(dev_idx, DEVICE_DIRECTORY_WARNING.to_string());
            }
            Warning::TestWithUnavailableProtocol(protocol) => {
                let proto_idx = lines
                    .iter()
                    .position(|l| l.contains(&*protocol))
                    .expect("uses must have protocol listed if we're warning about it");
                lines.insert(proto_idx, UNAVAILABLE_TEST_PROTOCOL_WARNING.to_string());
            }
        }
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// VERSION_HISTORY is an array of all the known SDK versions.
pub const VERSION_HISTORY: &[Version] = &version_history_macro::declare_version_history!();

/// Version is a mapping between the supported API level and the ABI revisions.
///
/// See https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0002_platform_versioning for more
/// details.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Clone)]
pub struct Version {
    /// The API level denotes a set of APIs available when building an application for a given
    /// release of the FUCHSIA IDK.
    pub api_level: u64,

    /// The ABI revision denotes the semantics of the Fuchsia System Interface that an application
    /// expects the platform to provide.
    pub abi_revision: u64,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_proc_macro_worked() {
        let expected = version_history_shared::version_history()
            .unwrap()
            .into_iter()
            .map(|version| Version {
                api_level: version.api_level,
                abi_revision: version.abi_revision,
            })
            .collect::<Vec<_>>();

        assert_eq!(expected, VERSION_HISTORY);
    }
}

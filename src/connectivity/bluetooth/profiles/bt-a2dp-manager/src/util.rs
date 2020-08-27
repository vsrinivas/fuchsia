// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_bluetooth_a2dp::Role, fuchsia_component::fuchsia_single_component_package_url};

/// The URLs for components required by the A2DP Sink role.
static SINK_COMPONENT_URLS: &'static [&'static str] =
    &[fuchsia_single_component_package_url!("bt-a2dp-sink")];

/// The URLs for components required by the A2DP Source role.
static SOURCE_COMPONENT_URLS: &'static [&'static str] = &[
    fuchsia_single_component_package_url!("bt-a2dp-source"),
    fuchsia_single_component_package_url!("bt-avrcp-target"),
];

/// Returns a slice of URLs required for `role`.
pub fn into_urls(role: Role) -> &'static [&'static str] {
    match role {
        Role::Source => SOURCE_COMPONENT_URLS,
        Role::Sink => SINK_COMPONENT_URLS,
    }
}

/// Helper function that fulfills the role of the `Display` trait for the foreign type `Role`.
pub fn to_display_str(role: Role) -> &'static str {
    match role {
        Role::Source => "Source",
        Role::Sink => "Sink",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn into_urls_returns_expected_number_of_packages() {
        assert_eq!(into_urls(Role::Source).len(), 2);
        assert_eq!(into_urls(Role::Sink).len(), 1);
    }

    #[test]
    fn role_to_display_str() {
        let role = Role::Source;
        assert_eq!(to_display_str(role), "Source");

        let role = Role::Sink;
        assert_eq!(to_display_str(role), "Sink");
    }
}

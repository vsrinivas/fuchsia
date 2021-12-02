// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::channel::ChannelConfigs,
    omaha_client::{app_set::AppSet, common::App},
};

pub struct FuchsiaAppSet {
    system_app: App,
    package_groups: Vec<PackageGroup>,
}

impl FuchsiaAppSet {
    pub fn new(system_app: App) -> Self {
        Self { system_app, package_groups: vec![] }
    }

    #[allow(dead_code)]
    pub fn add_package_group(&mut self, group: PackageGroup) {
        self.package_groups.push(group);
    }

    /// Get the system app id.
    pub fn get_system_app_id(&self) -> &str {
        &self.system_app.id
    }

    /// Get the system product id.
    /// Returns empty string if product id not set for the system app.
    pub fn get_system_product_id(&self) -> &str {
        self.system_app.extra_fields.get("product_id").map(|s| &**s).unwrap_or("")
    }

    /// Get the current channel name from cohort name, returns empty string if no cohort name set
    /// for the app.
    pub fn get_system_current_channel(&self) -> &str {
        self.system_app.get_current_channel()
    }

    /// Get the target channel name from cohort hint, fallback to current channel if no hint.
    pub fn get_system_target_channel(&self) -> &str {
        self.system_app.get_target_channel()
    }

    /// Set the cohort hint of system app to |channel| and |id|.
    pub fn set_system_target_channel(&mut self, channel: Option<String>, id: Option<String>) {
        self.system_app.set_target_channel(channel, id);
    }
}

impl AppSet for FuchsiaAppSet {
    fn get_apps(&self) -> Vec<App> {
        std::iter::once(&self.system_app)
            .chain(self.package_groups.iter().map(|pg| &pg.app))
            .cloned()
            .collect()
    }
    fn iter_mut_apps(&mut self) -> Box<dyn Iterator<Item = &mut App> + '_> {
        Box::new(
            std::iter::once(&mut self.system_app)
                .chain(self.package_groups.iter_mut().map(|pg| &mut pg.app)),
        )
    }
}

pub struct PackageGroup {
    app: App,
    #[allow(dead_code)]
    channel_configs: Option<ChannelConfigs>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_apps() {
        let app = App::builder("some_id", [0, 1]).build();
        let mut app_set = FuchsiaAppSet::new(app.clone());
        assert_eq!(app_set.get_apps(), vec![app.clone()]);

        let package_group_app = App::builder("package_id", [5]).build();
        let package_group = PackageGroup { app: package_group_app.clone(), channel_configs: None };
        app_set.add_package_group(package_group);
        assert_eq!(app_set.get_apps(), vec![app, package_group_app]);
    }

    #[test]
    fn test_iter_mut_apps() {
        let app = App::builder("id1", [1]).build();
        let mut app_set = FuchsiaAppSet::new(app);
        let package_group =
            PackageGroup { app: App::builder("package_id", [5]).build(), channel_configs: None };
        app_set.add_package_group(package_group);

        for app in app_set.iter_mut_apps() {
            app.id += "_mutated";
        }
        assert_eq!(
            app_set.get_apps(),
            vec![
                App::builder("id1_mutated", [1]).build(),
                App::builder("package_id_mutated", [5]).build()
            ]
        );
    }
}

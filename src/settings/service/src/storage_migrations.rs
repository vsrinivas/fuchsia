// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::migration::{MigrationError, MigrationManager, MigrationManagerBuilder};
use crate::policy::PolicyType;
use fidl_fuchsia_io::DirectoryProxy;
use fidl_fuchsia_stash::StoreProxy;
use std::collections::HashSet;
use v1653667208_light_migration::V1653667208LightMigration;
use v1653667210_light_migration_teardown::V1653667210LightMigrationTeardown;

mod v1653667208_light_migration;
mod v1653667210_light_migration_teardown;

pub(crate) fn register_migrations(
    settings: &HashSet<SettingType>,
    _policies: &HashSet<PolicyType>,
    migration_dir: DirectoryProxy,
    store_proxy: StoreProxy,
) -> Result<MigrationManager, MigrationError> {
    let mut builder = MigrationManagerBuilder::new();
    builder.set_migration_dir(migration_dir);
    if settings.contains(&SettingType::Light) {
        builder.register(V1653667208LightMigration(store_proxy.clone()))?;
        builder.register(V1653667210LightMigrationTeardown(store_proxy))?;
    }
    Ok(builder.build())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy;
    use fidl::{endpoints::ServerEnd, Vmo};
    use fidl_fuchsia_io::{DirectoryMarker, OpenFlags};
    use fidl_fuchsia_stash::StoreMarker;
    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use std::collections::HashMap;
    use std::sync::Arc;
    use vfs::directory::entry::DirectoryEntry;
    use vfs::directory::mutable::simple::tree_constructor;
    use vfs::execution_scope::ExecutionScope;
    use vfs::file::vmo::asynchronous::test_utils::simple_init_vmo_with_capacity;
    use vfs::file::vmo::read_write;

    // Run migration registration with all settings and policies turned on so we can ensure there's
    // no issues with registering any of the migrations.
    #[fasync::run_until_stalled(test)]
    async fn ensure_unique_ids() {
        let mut settings = HashSet::new();
        let _ = settings.insert(SettingType::Accessibility);
        let _ = settings.insert(SettingType::Audio);
        let _ = settings.insert(SettingType::Display);
        let _ = settings.insert(SettingType::DoNotDisturb);
        let _ = settings.insert(SettingType::FactoryReset);
        let _ = settings.insert(SettingType::Input);
        let _ = settings.insert(SettingType::Intl);
        let _ = settings.insert(SettingType::Keyboard);
        let _ = settings.insert(SettingType::Light);
        let _ = settings.insert(SettingType::LightSensor);
        let _ = settings.insert(SettingType::NightMode);
        let _ = settings.insert(SettingType::Privacy);
        let _ = settings.insert(SettingType::Setup);
        let mut policies = HashSet::new();
        let _ = policies.insert(PolicyType::Audio);
        let (directory_proxy, _) = create_proxy::<DirectoryMarker>().unwrap();
        let (store_proxy, _) =
            create_proxy::<StoreMarker>().expect("failed to create proxy for stash");
        if let Err(e) = register_migrations(&settings, &policies, directory_proxy, store_proxy) {
            panic!("Unable to register migrations: Err({:?})", e);
        }
    }

    /// Serve a directory from a virtual file system.
    pub(crate) fn serve_vfs_dir(
        root: Arc<impl DirectoryEntry>,
    ) -> (DirectoryProxy, Arc<Mutex<HashMap<String, Vmo>>>) {
        let vmo_map = Arc::new(Mutex::new(HashMap::new()));
        let fs_scope = ExecutionScope::build()
            .entry_constructor(tree_constructor(move |_, _| {
                Ok(read_write(simple_init_vmo_with_capacity(b"", 1024)))
            }))
            .new();
        let (client, server) = create_proxy::<DirectoryMarker>().unwrap();
        root.open(
            fs_scope,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server.into_channel()),
        );
        (client, vmo_map)
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg_attr(not(test), allow(dead_code))]

//! The `migration` module exposes structs and traits that can be used to write data migrations for
//! the settings service.
//!
//! # Example:
//! ```
//! let mut builder = MigrationManagerBuilder::new();
//! builder.register(1649199438, Box::new(|file_proxy_generator| async move {
//!     let file_proxy = (file_proxy_generator)("setting_name").await?;
//! }))?;
//! builder.set_dir_proxy(dir_proxy);
//! let migration_manager = builder.build();
//! migration_manager.run_migrations().await?;
//! ```

use anyhow::{bail, Context, Error};
use async_trait::async_trait;
use fidl_fuchsia_io::{DirectoryProxy, FileProxy, UnlinkOptions};
use files_async::{readdir, DirEntry, DirentKind};
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use io_util::node::OpenError;
use io_util::OpenFlags;
use std::collections::BTreeMap;

/// Errors that can occur during an individual migration.
#[derive(thiserror::Error, Debug)]
pub(crate) enum MigrationError {
    #[error("The data to be migrated from does not exist")]
    NoData,
    #[error("The disk is full so the migration cannot be performed")]
    DiskFull,
    #[error("An unrecoverable error occurred")]
    Unrecoverable(#[source] Error),
}

impl From<Error> for MigrationError {
    fn from(e: Error) -> Self {
        MigrationError::Unrecoverable(e)
    }
}

pub(crate) struct MigrationManager {
    migrations: BTreeMap<u64, Box<dyn Migration>>,
    dir_proxy: DirectoryProxy,
}

pub(crate) struct MigrationManagerBuilder {
    migrations: BTreeMap<u64, Box<dyn Migration>>,
    dir_proxy: Option<DirectoryProxy>,
}

const MIGRATION_FILE_NAME: &str = "migrations.txt";
const TMP_MIGRATION_FILE_NAME: &str = "tmp_migrations.txt";

impl MigrationManagerBuilder {
    /// Construct a new [MigrationManagerBuilder]
    pub(crate) fn new() -> Self {
        Self { migrations: BTreeMap::new(), dir_proxy: None }
    }

    /// Register a `migration` with a unique `id`. This will fail if another migration with the same
    /// `id` is already registered.
    pub(crate) fn register(&mut self, migration: impl Migration + 'static) -> Result<(), Error> {
        self.register_internal(Box::new(migration))
    }

    fn register_internal(&mut self, migration: Box<dyn Migration>) -> Result<(), Error> {
        let id = migration.id();
        if self.migrations.insert(id, migration).is_some() {
            bail!("migration with id {} already registered", id)
        }
        Ok(())
    }

    /// Set the directory where migration data will be tracked.
    pub(crate) fn set_migration_dir(&mut self, dir_proxy: DirectoryProxy) {
        self.dir_proxy = Some(dir_proxy);
    }

    /// Construct a [MigrationManager] from the registered migrations and directory proxy. This
    /// method will panic if no directory proxy is registered.
    pub(crate) fn build(self) -> MigrationManager {
        MigrationManager {
            migrations: self.migrations,
            dir_proxy: self.dir_proxy.expect("dir proxy must be provided"),
        }
    }
}

impl MigrationManager {
    /// Run all registered migrations. Will return an error if the migrations could not be
    /// completed.
    pub(crate) async fn run_migrations(mut self) -> Result<(), MigrationError> {
        let last_migration = {
            let migration_file = io_util::directory::open_file(
                &self.dir_proxy,
                MIGRATION_FILE_NAME,
                OpenFlags::NOT_DIRECTORY | OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            )
            .await;
            match migration_file {
                Err(e) => {
                    if let OpenError::OpenError(zx::Status::NOT_FOUND) = e {
                        if let Some(last_migration) = self
                            .initialize_migrations()
                            .await
                            .context("failed to initialize migrations")?
                        {
                            LastMigration { migration_id: last_migration }
                        } else {
                            // There are no migrations to run.
                            return Ok(());
                        }
                    } else {
                        return Err(Error::from(e).context("failed to load migrations").into());
                    }
                }
                Ok(migration_file) => {
                    let migration_data = io_util::read_file(&migration_file)
                        .await
                        .context("failed to read migrations.txt")?;
                    LastMigration {
                        migration_id: migration_data
                            .parse()
                            .context("Failed to parse migration id from migrations.txt")?,
                    }
                }
            }
        };

        // Track the last migration so we know whether to update the migration file.
        let mut new_last_migration = last_migration.migration_id;

        // Skip over migrations already previously completed.
        for (id, migration) in self
            .migrations
            .into_iter()
            .skip_while(|&(id, _)| id != last_migration.migration_id)
            .skip(1)
        {
            let file_generator = FileGenerator::new(new_last_migration, id, &self.dir_proxy);
            new_last_migration = id;
            migration.migrate(file_generator).await.map_err(|e| match e {
                MigrationError::NoData => {
                    fx_log_err!("Missing data necessary for running migration {id}");
                    MigrationError::NoData
                }
                MigrationError::Unrecoverable(e) => {
                    MigrationError::Unrecoverable(e.context(format!("Migration {id} failed")))
                }
                _ => e,
            })?;
        }

        // If the last migration is newer than the previously recorded value, update the
        // migration file.
        if new_last_migration != last_migration.migration_id {
            // Write to a temporary file before renaming so we can ensure the update is atomic.
            let tmp_migration_file = io_util::directory::open_file(
                &self.dir_proxy,
                TMP_MIGRATION_FILE_NAME,
                OpenFlags::NOT_DIRECTORY | OpenFlags::CREATE | OpenFlags::RIGHT_WRITABLE,
            )
            .await
            .context("unable to create migrations file")?;
            io_util::write_file(&tmp_migration_file, &new_last_migration.to_string())
                .await
                .context("failed to write new migration number")?;
            drop(tmp_migration_file);

            if let Err(e) = io_util::directory::rename(
                &self.dir_proxy,
                TMP_MIGRATION_FILE_NAME,
                MIGRATION_FILE_NAME,
            )
            .await
            {
                return Err(match e {
                    io_util::node::RenameError::RenameError(zx::Status::NO_SPACE) => {
                        MigrationError::DiskFull
                    }
                    _ => Error::from(e).context("failed to rename tmp to migrations.txt").into(),
                });
            };
        }

        let file_suffix = format!("_{new_last_migration}.pfidl");
        let entries: Vec<DirEntry> = readdir(&self.dir_proxy).await.context("another error")?;
        let files = entries.into_iter().filter_map(|entry| {
            if let DirentKind::File = entry.kind {
                if entry.name == MIGRATION_FILE_NAME || entry.name.ends_with(&file_suffix) {
                    None
                } else {
                    Some(entry.name)
                }
            } else {
                None
            }
        });

        for file in files {
            self.dir_proxy
                .unlink(&file, UnlinkOptions::EMPTY)
                .await
                .context("failed to remove old file from file system")?
                .map_err(zx::Status::from_raw)
                .context("another error")?;
        }

        Ok(())
    }

    /// Runs the initial migration. If it fails due to a [MigrationError::NoData] error, then it
    /// will return the last migration to initialize all of the data sources.
    async fn initialize_migrations(&mut self) -> Result<Option<u64>, MigrationError> {
        // Try to run the first migration. If it fails because there's no data in stash then we can
        // use default values and skip to the final migration number. If it fails because the disk
        // is full, propagate the error up so the main client can decide how to gracefully fallback.
        let &id = if let Some(id) = self.migrations.keys().next() {
            id
        } else {
            return Ok(None);
        };
        let migration = self.migrations.remove(&id).unwrap();

        let file_generator = FileGenerator::new(0, id, &self.dir_proxy);

        let migration_result = migration.migrate(file_generator).await;
        if let Err(migration_error) = migration_result {
            match migration_error {
                MigrationError::NoData => {
                    // There was no previous data. We just need to use the default value for all
                    // data and use the most recent migration as the migration number.
                    return Ok(self.migrations.keys().last().copied());
                }
                MigrationError::DiskFull => return Err(MigrationError::DiskFull),
                MigrationError::Unrecoverable(e) => {
                    return Err(MigrationError::Unrecoverable(
                        e.context("Failed to run initial migration"),
                    ))
                }
            }
        }

        Ok(None)
    }
}

pub(crate) struct FileGenerator<'a> {
    old_id: u64,
    new_id: u64,
    dir_proxy: &'a DirectoryProxy,
}

#[allow(dead_code)]
impl<'a> FileGenerator<'a> {
    pub(crate) fn new(old_id: u64, new_id: u64, dir_proxy: &'a DirectoryProxy) -> Self {
        Self { old_id, new_id, dir_proxy }
    }

    pub(crate) async fn old_file(
        &self,
        file_name: impl AsRef<str>,
    ) -> Result<FileProxy, OpenError> {
        self.file(file_name.as_ref(), self.old_id).await
    }

    pub(crate) async fn new_file(
        &self,
        file_name: impl AsRef<str>,
    ) -> Result<FileProxy, OpenError> {
        self.file(file_name.as_ref(), self.new_id).await
    }

    async fn file(&self, file_name: &str, id: u64) -> Result<FileProxy, OpenError> {
        io_util::directory::open_file(
            self.dir_proxy,
            &format!("{file_name}_{id}.pfidl"),
            OpenFlags::NOT_DIRECTORY
                | OpenFlags::CREATE
                | OpenFlags::RIGHT_READABLE
                | OpenFlags::RIGHT_WRITABLE,
        )
        .await
    }
}

#[async_trait]
pub(crate) trait Migration {
    fn id(&self) -> u64;
    async fn migrate<'a>(&'a self, file_generator: FileGenerator<'a>)
        -> Result<(), MigrationError>;
    async fn cleanup<'a>(&'a self, file_generator: FileGenerator<'a>)
        -> Result<(), MigrationError>;
}

pub(crate) struct LastMigration {
    migration_id: u64,
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fidl::endpoints::{create_proxy, ServerEnd};
    use fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy};
    use fuchsia_async as fasync;
    use futures::future::BoxFuture;
    use futures::FutureExt;
    use io_util::OpenFlags;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use vfs::directory::entry::DirectoryEntry;
    use vfs::execution_scope::ExecutionScope;
    use vfs::file::vmo::asynchronous::{read_write, simple_init_vmo_resizable_with_capacity};
    use vfs::mut_pseudo_directory;

    #[async_trait]
    impl<T> Migration for (u64, T)
    where
        T: Fn(FileGenerator<'_>) -> BoxFuture<'static, Result<(), MigrationError>> + Send + Sync,
    {
        fn id(&self) -> u64 {
            self.0
        }

        async fn migrate<'a>(
            &'a self,
            file_generator: FileGenerator<'a>,
        ) -> Result<(), MigrationError> {
            (self.1)(file_generator).await
        }

        async fn cleanup<'a>(&'a self, _: FileGenerator<'a>) -> Result<(), MigrationError> {
            Ok(())
        }
    }

    #[test]
    fn cannot_register_same_id_twice() {
        let mut builder = MigrationManagerBuilder::new();
        const ID: u64 = 1;
        builder
            .register((ID, Box::new(|_: FileGenerator<'_>| async move { Ok(()) }.boxed())))
            .expect("should register once");
        let result = builder
            .register((ID, Box::new(|_: FileGenerator<'_>| async move { Ok(()) }.boxed())))
            .map_err(|e| format!("{e:}"));
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), "migration with id 1 already registered");
    }

    #[test]
    #[should_panic(expected = "dir proxy must be provided")]
    fn cannot_build_migration_manager_without_dir_proxy() {
        let builder = MigrationManagerBuilder::new();
        let _ = builder.build();
    }

    // Needs to be async to create the proxy and stream.
    #[fasync::run_until_stalled(test)]
    async fn can_build_migration_manager_without_migrations() {
        let (proxy, _) = fidl::endpoints::create_proxy_and_stream::<DirectoryMarker>().unwrap();
        let mut builder = MigrationManagerBuilder::new();
        builder.set_migration_dir(proxy);
        let _migration_manager = builder.build();
    }

    fn serve_vfs_dir(root: Arc<impl DirectoryEntry>) -> DirectoryProxy {
        let fs_scope = ExecutionScope::new();
        let (client, server) = create_proxy::<DirectoryMarker>().unwrap();
        root.open(
            fs_scope,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server.into_channel()),
        );
        client
    }

    // Test for initial migration
    #[fasync::run_until_stalled(test)]
    async fn if_no_migration_file_runs_initial_migration() {
        let fs = mut_pseudo_directory! {};
        let directory = serve_vfs_dir(fs);
        let mut builder = MigrationManagerBuilder::new();
        let migration_ran = Arc::new(AtomicBool::new(false));

        builder
            .register((
                1,
                Box::new({
                    let migration_ran = Arc::clone(&migration_ran);
                    move |_: FileGenerator<'_>| {
                        let migration_ran = Arc::clone(&migration_ran);
                        async move {
                            migration_ran.store(true, Ordering::SeqCst);
                            Ok(())
                        }
                        .boxed()
                    }
                }),
            ))
            .expect("can register");
        builder.set_migration_dir(directory);
        let migration_manager = builder.build();

        let result = migration_manager.run_migrations().await;
        assert_matches!(result, Ok(()));
    }

    #[fasync::run_until_stalled(test)]
    async fn if_migration_file_and_up_to_date_no_migrations_run() {
        let fs = mut_pseudo_directory! {
            MIGRATION_FILE_NAME => read_write(
                simple_init_vmo_resizable_with_capacity(b"1", 1)
            ),
            "test_1.pfidl" => read_write(
                simple_init_vmo_resizable_with_capacity(b"", 1)
            ),
        };
        let directory = serve_vfs_dir(fs);
        let mut builder = MigrationManagerBuilder::new();
        let migration_ran = Arc::new(AtomicBool::new(false));

        builder
            .register((
                1,
                Box::new({
                    let migration_ran = Arc::clone(&migration_ran);
                    move |_: FileGenerator<'_>| {
                        let migration_ran = Arc::clone(&migration_ran);
                        async move {
                            migration_ran.store(true, Ordering::SeqCst);
                            Ok(())
                        }
                        .boxed()
                    }
                }),
            ))
            .expect("can register");
        builder.set_migration_dir(directory);
        let migration_manager = builder.build();

        let result = migration_manager.run_migrations().await;
        assert_matches!(result, Ok(()));
        assert!(!migration_ran.load(Ordering::SeqCst));
    }

    #[fasync::run_until_stalled(test)]
    async fn migration_file_exists_but_missing_data() {
        let fs = mut_pseudo_directory! {
            MIGRATION_FILE_NAME => read_write(
                simple_init_vmo_resizable_with_capacity(b"1", 1)
            ),
        };
        let directory = serve_vfs_dir(fs);
        let mut builder = MigrationManagerBuilder::new();
        let initial_migration_ran = Arc::new(AtomicBool::new(false));

        builder
            .register((
                1,
                Box::new({
                    let initial_migration_ran = Arc::clone(&initial_migration_ran);
                    move |_: FileGenerator<'_>| {
                        let initial_migration_ran = Arc::clone(&initial_migration_ran);
                        async move {
                            initial_migration_ran.store(true, Ordering::SeqCst);
                            Ok(())
                        }
                        .boxed()
                    }
                }),
            ))
            .expect("can register");

        let second_migration_ran = Arc::new(AtomicBool::new(false));
        builder
            .register((
                2,
                Box::new({
                    let second_migration_ran = Arc::clone(&second_migration_ran);
                    move |_: FileGenerator<'_>| {
                        let second_migration_ran = Arc::clone(&second_migration_ran);
                        async move {
                            second_migration_ran.store(true, Ordering::SeqCst);
                            Err(MigrationError::NoData)
                        }
                        .boxed()
                    }
                }),
            ))
            .expect("can register");
        builder.set_migration_dir(directory);
        let migration_manager = builder.build();

        let result = migration_manager.run_migrations().await;
        assert_matches!(result, Err(MigrationError::NoData));
        assert!(!initial_migration_ran.load(Ordering::SeqCst));
        assert!(second_migration_ran.load(Ordering::SeqCst));
    }

    // TODO(fxbug.dev/91407) Test for disk full behavior
    // TODO(fxbug.dev/91407) Tests for power outage simulation (drop future after various write
    // stages)
    // TODO(fxbug.dev/91407) Test that migration file is accurate even after a partial failure, e.g.
    // simulate disk full after certain number of migrations)
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::migration::{FileGenerator, Migration, MigrationError};
use anyhow::{anyhow, Context};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_stash::StoreProxy;
use setui_metrics_registry::ActiveMigrationsMetricDimensionMigrationId as MigrationIdMetric;

const STASH_KEY: &str = "settings_factory_reset_info";

/// Deletes old Light settings data from stash.
pub(crate) struct V1668815249FactoryResetMigrationTeardown(pub(crate) StoreProxy);

#[async_trait::async_trait]
impl Migration for V1668815249FactoryResetMigrationTeardown {
    fn id(&self) -> u64 {
        1668815249
    }

    fn cobalt_id(&self) -> u32 {
        MigrationIdMetric::V1668815249 as u32
    }

    async fn migrate(&self, _: FileGenerator) -> Result<(), MigrationError> {
        let (stash_proxy, server_end) = create_proxy().expect("failed to create proxy for stash");
        self.0.create_accessor(false, server_end).expect("failed to create accessor for stash");
        stash_proxy.delete_value(STASH_KEY).context("failed to call delete_value")?;
        stash_proxy.commit().context("failed to commit deletion of old factory reset key")?;
        drop(stash_proxy);

        let (stash_proxy, server_end) = create_proxy().expect("failed to create proxy for stash");
        self.0.create_accessor(true, server_end).expect("failed to create accessor for stash");
        let value = stash_proxy.get_value(STASH_KEY).await.context("failed to call get_value")?;
        if value.is_some() {
            Err(MigrationError::Unrecoverable(anyhow!("failed to delete stash data")))
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::storage_migrations::tests::serve_vfs_dir;
    use assert_matches::assert_matches;
    use fidl_fuchsia_stash::{StoreAccessorRequest, StoreMarker, StoreRequest, Value};
    use fuchsia_async as fasync;
    use futures::StreamExt;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use vfs::mut_pseudo_directory;

    // Ensure the teardown deletes and commits the deletion of data from stash.
    #[fasync::run_until_stalled(test)]
    async fn v1668815249_factory_reset_migration_teardown_test() {
        let (store_proxy, server_end) =
            create_proxy::<StoreMarker>().expect("failed to create proxy for stash");
        let mut request_stream = server_end.into_stream().expect("Should be able to get stream");
        let commit_called = Arc::new(AtomicBool::new(false));
        let task = fasync::Task::spawn({
            let commit_called = Arc::clone(&commit_called);
            async move {
                let mut tasks = vec![];
                while let Some(Ok(request)) = request_stream.next().await {
                    if let StoreRequest::CreateAccessor { accessor_request, .. } = request {
                        let mut request_stream =
                            accessor_request.into_stream().expect("should be able to get stream");
                        tasks.push(fasync::Task::spawn({
                            let commit_called = Arc::clone(&commit_called);
                            async move {
                                while let Some(Ok(request)) = request_stream.next().await {
                                    match request {
                                        StoreAccessorRequest::DeleteValue { key, .. } => {
                                            assert_eq!(key, STASH_KEY);
                                        }
                                        StoreAccessorRequest::Commit { .. } => {
                                            commit_called.store(true, Ordering::SeqCst);
                                        }
                                        StoreAccessorRequest::GetValue { key, responder } => {
                                            assert_eq!(key, STASH_KEY);
                                            responder
                                                .send(None)
                                                .expect("should be able to send response");
                                        }
                                        _ => panic!("unexpected request: {:?}", request),
                                    }
                                }
                            }
                        }))
                    }
                }
                for task in tasks {
                    task.await
                }
            }
        });

        let migration = V1668815249FactoryResetMigrationTeardown(store_proxy);
        let fs = mut_pseudo_directory! {};
        let (directory, _vmo_map) = serve_vfs_dir(fs);
        let file_generator = FileGenerator::new(0, migration.id(), Clone::clone(&directory));
        assert_matches!(migration.migrate(file_generator).await, Ok(()));

        drop(migration);

        task.await;
        assert!(commit_called.load(Ordering::SeqCst));
    }

    // Ensure we report an unrecoverable error if we're unable to delete the data from stash.
    #[fasync::run_until_stalled(test)]
    async fn v1668815249_factory_reset_migration_teardown_commit_fails() {
        let (store_proxy, server_end) =
            create_proxy::<StoreMarker>().expect("failed to create proxy for stash");
        let mut request_stream = server_end.into_stream().expect("Should be able to get stream");
        let task = fasync::Task::spawn(async move {
            let mut tasks = vec![];
            while let Some(Ok(request)) = request_stream.next().await {
                if let StoreRequest::CreateAccessor { accessor_request, .. } = request {
                    let mut request_stream =
                        accessor_request.into_stream().expect("should be able to get stream");
                    tasks.push(fasync::Task::spawn(async move {
                        while let Some(Ok(request)) = request_stream.next().await {
                            match request {
                                StoreAccessorRequest::DeleteValue { .. } => {
                                    // no-op
                                }
                                StoreAccessorRequest::Commit { .. } => {
                                    // no-op
                                }
                                StoreAccessorRequest::GetValue { key, responder } => {
                                    assert_eq!(key, STASH_KEY);
                                    responder
                                        .send(Some(&mut Value::Stringval("data".to_owned())))
                                        .expect("should be able to send response");
                                }
                                _ => panic!("unexpected request: {:?}", request),
                            }
                        }
                    }))
                }
            }
            for task in tasks {
                task.await
            }
        });

        let migration = V1668815249FactoryResetMigrationTeardown(store_proxy);
        let fs = mut_pseudo_directory! {};
        let (directory, _vmo_map) = serve_vfs_dir(fs);
        let file_generator = FileGenerator::new(0, migration.id(), Clone::clone(&directory));
        let result = migration.migrate(file_generator).await;
        assert_matches!(result, Err(MigrationError::Unrecoverable(_)));
        assert!(format!("{:?}", result.unwrap_err()).contains("failed to delete stash data"));

        drop(migration);

        task.await;
    }
}

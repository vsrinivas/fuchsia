// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::light::types::LightInfo;
use crate::migration::{FileGenerator, Migration, MigrationError};
use anyhow::{anyhow, Context};
use fidl::encoding::encode_persistent;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_io::FileProxy;
use fidl_fuchsia_settings::LightGroup as LightGroupFidl;
use fidl_fuchsia_settings_storage::LightGroups;
use fidl_fuchsia_stash::{StoreProxy, Value};
use fuchsia_zircon as zx;
use settings_storage::fidl_storage::FidlStorageConvertible;
use setui_metrics_registry::ActiveMigrationsMetricDimensionMigrationId as MigrationIdMetric;

const LIGHT_KEY: &str = "settings_light_info";

/// Migrates Light settings data from stash and stores it in persistent fidl-serialized files.
pub(crate) struct V1653667208LightMigration(pub(crate) StoreProxy);

#[async_trait::async_trait]
impl Migration for V1653667208LightMigration {
    fn id(&self) -> u64 {
        1653667208
    }

    fn cobalt_id(&self) -> u32 {
        MigrationIdMetric::V1653667208 as u32
    }

    async fn migrate(&self, file_generator: FileGenerator) -> Result<(), MigrationError> {
        let (stash_proxy, server_end) = create_proxy().expect("failed to create proxy for stash");
        self.0.create_accessor(true, server_end).expect("failed to create accessor for stash");
        let value = stash_proxy.get_value(LIGHT_KEY).await.context("failed to call get_value")?;
        let str_json = match value {
            None => return Err(MigrationError::NoData),
            Some(value) => {
                if let Value::Stringval(str_json) = *value {
                    str_json
                } else {
                    return Err(MigrationError::Unrecoverable(anyhow!("data in incorrect format")));
                }
            }
        };
        let light_data = serde_json::from_str::<LightInfo>(&str_json)
            .context("failed to deserialize light data")?;

        let mut light_groups = LightGroups {
            groups: light_data
                .light_groups
                .into_values()
                .map(LightGroupFidl::from)
                .collect::<Vec<_>>(),
        };
        let file: FileProxy = file_generator
            .new_file(<LightInfo as FidlStorageConvertible>::KEY)
            .await
            .map_err(|file_error| match file_error {
                fuchsia_fs::node::OpenError::OpenError(status)
                    if status == zx::Status::NO_SPACE =>
                {
                    MigrationError::DiskFull
                }
                _ => Err::<(), _>(file_error)
                    .context("unable to open new_file: {:?}")
                    .unwrap_err()
                    .into(),
            })?;
        let encoded =
            encode_persistent(&mut light_groups).context("failed to serialize new fidl format")?;
        let _ = file.write(&encoded).await.context("file to call write")?.map_err(|e| {
            let status = zx::Status::from_raw(e);
            if status == zx::Status::NO_SPACE {
                MigrationError::DiskFull
            } else {
                MigrationError::Unrecoverable(anyhow!("failed to write migration: {:?}", e))
            }
        })?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::storage_migrations::tests::serve_vfs_dir;
    use assert_matches::assert_matches;
    use fidl_fuchsia_io::OpenFlags;
    use fidl_fuchsia_settings::{LightGroup, LightState, LightType, LightValue};
    use fidl_fuchsia_stash::{StoreAccessorRequest, StoreMarker, StoreRequest};
    use fidl_fuchsia_ui_types::ColorRgb;
    use fuchsia_async as fasync;
    use futures::StreamExt;
    use vfs::mut_pseudo_directory;

    // Ensure that failing to get data from stash (i.e. not out of space or not found) results in
    // an unrecoverable error.
    #[fasync::run_until_stalled(test)]
    async fn v1653667208_light_migration_error_getting_value() {
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
                            if let StoreAccessorRequest::GetValue { responder, .. } = request {
                                // Just ignore the responder so the client receives an error.
                                drop(responder);
                            } else {
                                panic!("unexpected request: {:?}", request);
                            }
                        }
                    }))
                }
            }
            for task in tasks {
                task.await
            }
        });

        let migration = V1653667208LightMigration(store_proxy);
        let fs = mut_pseudo_directory! {};
        let (directory, _vmo_map) = serve_vfs_dir(fs);
        let file_generator = FileGenerator::new(0, migration.id(), Clone::clone(&directory));
        let result = migration.migrate(file_generator).await;
        assert_matches!(result, Err(MigrationError::Unrecoverable(_)));
        let err = result.unwrap_err();
        assert!(format!("{err:?}").contains("failed to call get_value"));

        drop(migration);

        task.await;
    }

    // Ensure we see that no data is available when the data is not in stash.
    #[fasync::run_until_stalled(test)]
    async fn v1653667208_light_migration_no_data() {
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
                            if let StoreAccessorRequest::GetValue { responder, .. } = request {
                                responder.send(None).expect("should be able to send response");
                            } else {
                                panic!("unexpected request: {:?}", request);
                            }
                        }
                    }))
                }
            }
            for task in tasks {
                task.await
            }
        });

        let migration = V1653667208LightMigration(store_proxy);
        let fs = mut_pseudo_directory! {};
        let (directory, _vmo_map) = serve_vfs_dir(fs);
        let file_generator = FileGenerator::new(0, migration.id(), Clone::clone(&directory));
        assert_matches!(migration.migrate(file_generator).await, Err(MigrationError::NoData));

        drop(migration);

        task.await;
    }

    // Ensure we can properly migrate the original json data in stash over to persistent fidl.
    #[fasync::run_until_stalled(test)]
    async fn v1653667208_light_migration_test() {
        let (store_proxy, server_end) =
            create_proxy::<StoreMarker>().expect("failed to create proxy for stash");
        let mut request_stream = server_end.into_stream().expect("Should be able to get stream");
        let task = fasync::Task::spawn(async move {
            let mut tasks = vec![];
            while let Some(Ok(request)) = request_stream.next().await {
                if let StoreRequest::CreateAccessor { read_only, accessor_request, .. } = request {
                    assert!(read_only);
                    let mut request_stream =
                        accessor_request.into_stream().expect("should be able to get stream");
                    tasks.push(fasync::Task::spawn(async move {
                        while let Some(Ok(request)) = request_stream.next().await {
                            if let StoreAccessorRequest::GetValue { key, responder } = request {
                                assert_eq!(key, LIGHT_KEY);
                                responder
                                    .send(Some(&mut Value::Stringval(
                                        r#"{
                                            "light_groups":{
                                                "abc":{
                                                    "name":"abc",
                                                    "enabled":false,
                                                    "light_type":"Brightness",
                                                    "lights":[{
                                                        "value": {
                                                            "Brightness": 0.5
                                                        }
                                                    }],
                                                    "hardware_index":[],
                                                    "disable_conditions":[]
                                                },
                                                "def":{
                                                    "name":"def",
                                                    "enabled":true,
                                                    "light_type":"Rgb",
                                                    "lights":[{
                                                        "value": {
                                                            "Rgb": {
                                                                "red": 1.0,
                                                                "green": 0.5,
                                                                "blue": 0.0
                                                            }
                                                        }
                                                    }],
                                                    "hardware_index":[1, 2, 3],
                                                    "disable_conditions":[]
                                                },
                                                "ghi":{
                                                    "name":"ghi",
                                                    "enabled":false,
                                                    "light_type":"Simple",
                                                    "lights":[{
                                                        "value": {
                                                            "Simple": true
                                                        }
                                                    }],
                                                    "hardware_index":[1],
                                                    "disable_conditions":[]
                                                },
                                                "jkl":{
                                                    "name":"jkl",
                                                    "enabled":false,
                                                    "light_type":"Simple",
                                                    "lights":[{
                                                        "value": null
                                                    }],
                                                    "hardware_index":[1],
                                                    "disable_conditions":["MicSwitch"]
                                                }
                                            }
                                        }"#
                                        .to_owned(),
                                    )))
                                    .expect("should be able to respond");
                            } else {
                                panic!("unexpected request: {:?}", request);
                            }
                        }
                    }))
                }
            }
            for task in tasks {
                task.await
            }
        });

        let migration = V1653667208LightMigration(store_proxy);
        let fs = mut_pseudo_directory! {};
        let (directory, _vmo_map) = serve_vfs_dir(fs);
        let file_generator = FileGenerator::new(0, migration.id(), Clone::clone(&directory));
        assert_matches!(migration.migrate(file_generator).await, Ok(()));

        let file = fuchsia_fs::directory::open_file(
            &directory,
            "light_info_1653667208.pfidl",
            OpenFlags::RIGHT_READABLE,
        )
        .await
        .expect("file should exist");
        let LightGroups { groups } = fuchsia_fs::file::read_fidl::<LightGroups>(&file)
            .await
            .expect("should be able to read and deserialize light groups");
        assert_eq!(groups.len(), 4);
        assert!(groups.contains(&LightGroup {
            name: Some("abc".to_owned()),
            enabled: Some(false),
            type_: Some(LightType::Brightness),
            lights: Some(vec![LightState {
                value: Some(LightValue::Brightness(0.5)),
                ..LightState::EMPTY
            }]),
            ..LightGroup::EMPTY
        }));
        assert!(groups.contains(&LightGroup {
            name: Some("def".to_owned()),
            enabled: Some(true),
            type_: Some(LightType::Rgb),
            lights: Some(vec![LightState {
                value: Some(LightValue::Color(ColorRgb { red: 1.0, green: 0.5, blue: 0.0 })),
                ..LightState::EMPTY
            }]),
            ..LightGroup::EMPTY
        }));
        assert!(groups.contains(&LightGroup {
            name: Some("ghi".to_owned()),
            enabled: Some(false),
            type_: Some(LightType::Simple),
            lights: Some(vec![LightState {
                value: Some(LightValue::On(true)),
                ..LightState::EMPTY
            }]),
            ..LightGroup::EMPTY
        }));
        assert!(groups.contains(&LightGroup {
            name: Some("jkl".to_owned()),
            enabled: Some(false),
            type_: Some(LightType::Simple),
            lights: Some(vec![LightState { value: None, ..LightState::EMPTY }]),
            ..LightGroup::EMPTY
        }));

        drop(migration);

        task.await;
    }
}

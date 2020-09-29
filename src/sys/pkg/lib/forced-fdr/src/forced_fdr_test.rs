// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_recovery::FactoryResetRequestStream,
    fidl_fuchsia_update_channel::ProviderRequestStream,
    fuchsia_async as fasync,
    futures::prelude::*,
    parking_lot::Mutex,
    serde_json::json,
    std::{fs, sync::Arc},
    tempfile::TempDir,
};

fn spawn_update_info_with_channel(mut stream: ProviderRequestStream, channel: String) {
    fasync::Task::spawn(async move {
        let req = stream
            .try_next()
            .await
            .expect("Failed to get request from stream")
            .expect("Failed to get request from stream");

        let responder = req.into_get_current().expect("Got unexpected Provider request.");
        responder.send(&channel).expect("Failed to send response");
    })
    .detach();
}

fn spawn_factory_reset(mut stream: FactoryResetRequestStream) -> Arc<Mutex<i32>> {
    let call_count = Arc::new(Mutex::new(0));
    let ret = call_count.clone();
    fasync::Task::spawn(async move {
        let req = stream
            .try_next()
            .await
            .expect("Failed to get request from stream")
            .expect("Failed to get request from stream");

        let responder = req.into_reset().expect("Got unexpected FactoryReset request.");
        responder.send(0).expect("Failed to send response");

        *call_count.lock() += 1;
    })
    .detach();

    ret
}

fn spawn(dir: &TempDir, channel: &str) -> (ForcedFDR, Arc<Mutex<i32>>) {
    let (mock, info_stream, fdr_stream) =
        ForcedFDR::new_mock(dir.path().to_path_buf(), dir.path().to_path_buf());

    spawn_update_info_with_channel(info_stream, channel.into());

    let fdr_call_count = spawn_factory_reset(fdr_stream);

    (mock, fdr_call_count)
}

fn write_config_file(dir: &TempDir, contents: String) {
    fs::write(dir.path().join("forced-fdr-channel-indices.config"), contents)
        .expect("write forced-fdr-channel-indices.config")
}

fn write_config_channels(dir: &TempDir, items: Vec<(&str, i32)>) {
    let map: HashMap<&str, i32> = items.iter().cloned().collect();

    let json = json!({
        "version": "1",
        "content": {
            "channel_indices": map
        }
    });

    write_config_file(dir, json.to_string())
}

fn write_stored_index(dir: &TempDir, index: StoredIndex) {
    let path = dir.path().join("stored-index.json");
    let contents = serde_json::to_string(&index).expect("serialize StoredIndex to string");
    fs::write(path, contents).expect("write stored index")
}

fn assert_factory_reset_triggered(call_count: Arc<Mutex<i32>>) {
    let counter = call_count.lock();
    assert_eq!(*counter, 1);
}

fn assert_factory_reset_not_triggered(call_count: Arc<Mutex<i32>>) {
    let counter = call_count.lock();
    assert_eq!(*counter, 0);
}

fn assert_index_written(dir: &TempDir, index: StoredIndex) {
    let path = dir.path().join("stored-index.json");
    let actual = fs::read_to_string(path).expect("File should be written");
    let expected = serde_json::to_string(&index).expect("serialize StoredIndex to string");
    assert_eq!(actual, expected)
}

#[fasync::run_singlethreaded(test)]
async fn test_it_fdrs_nominally() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 44)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());
    assert_factory_reset_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_fdrs_nominally_at_boundry() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 44)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 43 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());
    assert_factory_reset_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_does_not_fdr_when_equal_index() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 12), ("channel-two", 17)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 17 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_does_not_fdr_when_greater_index() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 17)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_does_not_fdr_when_channel_not_in_list() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 44), ("channel-two", 29)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-three".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-three");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_writes_stored_file_when_missing() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 44)]);

    // Skipping: write_stored_index(..);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 44 };
    assert_index_written(&dir, stored_index);

    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_writes_stored_file_when_file_empty() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 56)]);

    // Write empty file
    let path = dir.path().join("stored-index.json");
    fs::write(path, "").expect("write stored index");

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 56 };
    assert_index_written(&dir, stored_index);

    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_writes_stored_file_when_file_invalid() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 63)]);

    // Write invalid file
    let path = dir.path().join("stored-index.json");
    fs::write(path, "SOME INVALID \n\ntype of file []{}//\\123456768790)(*&^%$#@!")
        .expect("write stored index");

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 63 };
    assert_index_written(&dir, stored_index);

    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_skips_fdr_when_config_invalid() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_file(&dir, "SOME INVALID \n\ntype of file []{}//\\123456768790)(*&^%$#@!".into());

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_skips_fdr_when_config_empty() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_file(&dir, "".into());

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_skips_fdr_when_config_missing() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    // Skipping: write_config_file(..)

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_skips_fdr_when_config_channel_list_empty() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_skips_fdr_when_channel_unavailable() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 44)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, _, fdr_stream) =
        ForcedFDR::new_mock(dir.path().to_path_buf(), dir.path().to_path_buf());

    // Skipping: spawn_update_info_with_channel(..);

    let fdr_call_count = spawn_factory_reset(fdr_stream);

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_err());
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_overwrites_stored_index_on_channel_change() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 19), ("channel-two", 18)]);

    let stored_index = StoredIndex::Version1 { channel: "channel-two".into(), index: 18 };
    write_stored_index(&dir, stored_index);

    let (mock, fdr_call_count) = spawn(&dir, "channel-one");

    // Act
    let result = run(mock).await;

    // Assert
    let stored_index = StoredIndex::Version1 { channel: "channel-one".into(), index: 19 };
    assert_index_written(&dir, stored_index);

    assert!(result.is_ok());
    assert_factory_reset_not_triggered(fdr_call_count);
}

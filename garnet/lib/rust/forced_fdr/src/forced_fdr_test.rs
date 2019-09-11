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
    fasync::spawn(async move {
        let req = stream
            .try_next()
            .await
            .expect("Failed to get request from stream")
            .expect("Failed to get request from stream");

        let responder = req.into_get_current().expect("Got unexpected Provider request.");
        responder.send(&channel).expect("Failed to send response");
    });
}

fn spawn_factory_reset(mut stream: FactoryResetRequestStream) -> Arc<Mutex<i32>> {
    let call_count = Arc::new(Mutex::new(0));
    let ret = call_count.clone();
    fasync::spawn(async move {
        let req = stream
            .try_next()
            .await
            .expect("Failed to get request from stream")
            .expect("Failed to get request from stream");

        let responder = req.into_reset().expect("Got unexpected FactoryReset request.");
        responder.send(0).expect("Failed to send response");

        *call_count.lock() += 1;
    });

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

fn write_stored_file(dir: &TempDir, contents: String) {
    fs::write(dir.path().join("previous-forced-fdr-index"), contents)
        .expect("write previous-forced-fdr-index")
}

fn write_stored_index(dir: &TempDir, index: i32) {
    write_stored_file(dir, index.to_string())
}

fn assert_factory_reset_triggered(call_count: Arc<Mutex<i32>>) {
    let counter = call_count.lock();
    assert_eq!(*counter, 1);
}

fn assert_factory_reset_not_triggered(call_count: Arc<Mutex<i32>>) {
    let counter = call_count.lock();
    assert_eq!(*counter, 0);
}

fn assert_index_written(dir: &TempDir, index: i32) {
    let written = fs::read_to_string(dir.path().join("previous-forced-fdr-index"))
        .expect("File should be written");
    assert_eq!(written, index.to_string())
}

#[fasync::run_singlethreaded(test)]
async fn test_it_fdrs_nominally() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 44)]);

    write_stored_index(&dir, 18);

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

    write_stored_index(&dir, 43);

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

    write_stored_index(&dir, 17);

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

    write_stored_index(&dir, 18);

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

    write_stored_index(&dir, 18);

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
    assert_index_written(&dir, 44);
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_writes_stored_file_when_file_empty() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 56)]);

    write_stored_file(&dir, "".into());

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());
    assert_index_written(&dir, 56);
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_writes_stored_file_when_file_invalid() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_channels(&dir, vec![("channel-one", 29), ("channel-two", 63)]);

    write_stored_file(&dir, "SOME INVALID \n\ntype of file []{}//\\123456768790)(*&^%$#@!".into());

    let (mock, fdr_call_count) = spawn(&dir, "channel-two");

    // Act
    let result = run(mock).await;

    // Assert
    assert!(result.is_ok());
    assert_index_written(&dir, 63);
    assert_factory_reset_not_triggered(fdr_call_count);
}

#[fasync::run_singlethreaded(test)]
async fn test_it_skips_fdr_when_config_invalid() {
    // Setup
    let dir = TempDir::new().expect("create tempdir");

    write_config_file(&dir, "SOME INVALID \n\ntype of file []{}//\\123456768790)(*&^%$#@!".into());

    write_stored_index(&dir, 18);

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

    write_stored_index(&dir, 18);

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

    write_stored_index(&dir, 18);

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

    write_stored_index(&dir, 18);

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

    write_stored_index(&dir, 18);

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

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Result},
    fidl_fuchsia_test_manager as test_manager,
    futures::channel::{mpsc, oneshot},
    futures::{pin_mut, select, FutureExt, SinkExt},
};

pub type LaunchResult = std::result::Result<(), test_manager::LaunchError>;

pub async fn handle_run_events(
    run_proxy: test_manager::RunControllerProxy,
    mut artifact_sender: mpsc::UnboundedSender<test_manager::Artifact>,
    kill_receiver: oneshot::Receiver<()>,
) -> Result<()> {
    let kill_fut = kill_receiver.fuse();
    pin_mut!(kill_fut);
    loop {
        let events_fut = run_proxy.get_events().fuse();
        pin_mut!(events_fut);
        let events = select! {
            result = kill_fut => {
                if result.is_ok() {
                    run_proxy.kill().context("fuchsia.test.manager.RunController/Kill")?;
                }
                break;
            }
            result = events_fut => result.context("fuchsia.test.manager.RunController/GetEvents")
        }?;
        if events.is_empty() {
            break;
        }
        for event in events {
            match event.payload {
                Some(test_manager::RunEventPayload::Artifact(artifact)) => {
                    artifact_sender.send(artifact).await.context("failed to send run artifact")?;
                }
                _ => {}
            };
        }
    }
    Ok(())
}

pub async fn handle_suite_events(
    suite_proxy: test_manager::SuiteControllerProxy,
    mut artifact_sender: mpsc::UnboundedSender<test_manager::Artifact>,
    start_sender: oneshot::Sender<LaunchResult>,
) -> Result<()> {
    // Wrap |start_sender| in an option to enforce using it at most once.
    let mut start_sender = Some(start_sender);
    loop {
        let result = suite_proxy
            .get_events()
            .await
            .context("fuchsia.test.manager.SuiteController/GetEvents")?;
        let events = match result {
            Ok(events) => events,
            Err(e) => {
                if let Some(start_sender) = start_sender.take() {
                    start_sender
                        .send(Err(e))
                        .map_err(|_| anyhow!("failed to send launch error"))?;
                }
                break;
            }
        };
        if events.is_empty() {
            break;
        }
        for event in events {
            match event.payload {
                Some(test_manager::SuiteEventPayload::SuiteStarted(_)) => {
                    if let Some(start_sender) = start_sender.take() {
                        start_sender
                            .send(Ok(()))
                            .map_err(|_| anyhow!("failed to send launch result"))?;
                    }
                }
                Some(test_manager::SuiteEventPayload::SuiteArtifact(suite_artifact)) => {
                    artifact_sender
                        .send(suite_artifact.artifact)
                        .await
                        .context("failed to send suite artifact")?;
                }
                Some(test_manager::SuiteEventPayload::CaseArtifact(case_artifact)) => {
                    artifact_sender
                        .send(case_artifact.artifact)
                        .await
                        .context("failed to send case artifact")?;
                }
                _ => {}
            };
        }
    }
    Ok(())
}

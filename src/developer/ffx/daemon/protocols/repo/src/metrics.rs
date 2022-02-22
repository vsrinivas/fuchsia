// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    analytics::add_custom_event,
    fidl_fuchsia_developer_bridge_ext::RepositorySpec,
    fuchsia_async::{self, TimeoutExt as _},
    std::{collections::BTreeMap, time::Duration},
};

const CATEGORY: &str = "ffx_daemon_repo";

async fn add_event(action: &'static str, custom_dimensions: BTreeMap<&'static str, String>) {
    let analytics_task = fuchsia_async::Task::local(async move {
        match add_custom_event(Some(CATEGORY), Some(&action), None, custom_dimensions).await {
            Ok(_) => {}
            Err(err) => {
                log::error!("metrics submission failed: {}", err);
            }
        }
    });

    analytics_task
        .on_timeout(Duration::from_secs(2), || {
            log::error!("metrics submisson timed out");
        })
        .await;
}

pub(crate) async fn server_mode_event(mode: &str) {
    let mut custom_dimensions = BTreeMap::new();
    custom_dimensions.insert("mode", mode.into());
    add_event("server.mode", custom_dimensions).await;
}

pub(crate) async fn server_started_event() {
    let mut custom_dimensions = BTreeMap::new();
    custom_dimensions.insert("result", "started".into());
    add_event("server.state", custom_dimensions).await;
}

pub(crate) async fn server_failed_to_start_event(msg: &str) {
    let mut custom_dimensions = BTreeMap::new();
    custom_dimensions.insert("result", "failed".into());
    custom_dimensions.insert("failure", msg.into());
    add_event("server.state", custom_dimensions).await;
}

pub(crate) async fn server_disabled_event() {
    let mut custom_dimensions = BTreeMap::new();
    custom_dimensions.insert("result", "disabled".into());
    add_event("server.state", custom_dimensions).await;
}

pub(crate) async fn add_repository_event(repo_spec: &RepositorySpec) {
    let repo_type = match repo_spec {
        RepositorySpec::FileSystem { .. } => "filesystem",
        RepositorySpec::Pm { .. } => "pm",
        RepositorySpec::Http { .. } => "http",
    };

    let mut custom_dimensions = BTreeMap::new();
    custom_dimensions.insert("type", repo_type.into());

    add_event("protocol.add-repository", custom_dimensions).await
}

pub(crate) async fn remove_repository_event() {
    add_event("protocol.remove-repository", BTreeMap::new()).await
}

pub(crate) async fn register_repository_event() {
    add_event("protocol.register-repository-to-target", BTreeMap::new()).await
}

pub(crate) async fn deregister_repository_event() {
    add_event("protocol.deregister-repository-from-target", BTreeMap::new()).await
}

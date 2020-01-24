// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_media::{
    Usage, UsageReporterMarker, UsageReporterRequest, UsageState, UsageWatcherProxy,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

/// An implementation of the UsageReporter for tests.
pub struct UsageReporterService {
    // Stores the UsageWatcher for the given Usage.
    usage_watchers: Arc<RwLock<HashMap<Usage, UsageWatcherProxy>>>,
}

impl UsageReporterService {
    pub fn new() -> Self {
        Self { usage_watchers: Arc::new(RwLock::new(HashMap::new())) }
    }

    // Set the state of a given usage. Calls the watcher's on_state_changed stored on the given
    // usage.
    pub async fn set_usage_state(&self, mut usage: Usage, mut usage_state: UsageState) {
        let usage_watchers = self.usage_watchers.read();
        match usage_watchers.get(&usage).clone() {
            None => fx_log_err!("Failed to read from usage watchers"),
            Some(watcher) => {
                watcher
                    .on_state_changed(&mut usage, &mut usage_state)
                    .await
                    .expect("Failed to set usage state");
            }
        }
    }
}

impl Service for UsageReporterService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == UsageReporterMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut usage_reporter_stream =
            ServerEnd::<UsageReporterMarker>::new(channel).into_stream()?;
        let usage_watchers_clone = self.usage_watchers.clone();

        fasync::spawn(async move {
            while let Some(req) = usage_reporter_stream.try_next().await.unwrap() {
                match req {
                    UsageReporterRequest::Watch {
                        usage,
                        usage_watcher,
                        control_handle: _control_handle,
                    } => {
                        usage_watchers_clone.write().insert(
                            usage,
                            usage_watcher.into_proxy().expect("Convert client end into proxy"),
                        );
                    }
                }
            }
        });

        Ok(())
    }
}

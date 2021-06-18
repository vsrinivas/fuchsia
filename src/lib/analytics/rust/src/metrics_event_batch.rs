// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::metrics_service::*;
use crate::INIT_ERROR;
use anyhow::{bail, Result};
use std::collections::BTreeMap;

const MAX_ENTRIES_EXCEEDED: &str = "There is a maximum of 20 events allowed in a batch.";

pub struct MetricsEventBatch {
    events: Vec<String>,
}

impl MetricsEventBatch {
    pub fn new() -> MetricsEventBatch {
        MetricsEventBatch { events: vec![] }
    }

    pub async fn add_launch_event(&mut self, args: Option<&str>) -> Result<()> {
        if self.full_batch() {
            bail!(MAX_ENTRIES_EXCEEDED);
        }
        let svc = METRICS_SERVICE.lock().await;
        svc.inner_add_custom_event(None, args, args, BTreeMap::new(), Some(self)).await
    }

    pub async fn add_custom_event(
        &mut self,
        category: Option<&str>,
        action: Option<&str>,
        label: Option<&str>,
        custom_dimensions: BTreeMap<&str, String>,
    ) -> Result<()> {
        if self.full_batch() {
            bail!(MAX_ENTRIES_EXCEEDED);
        }
        let svc = METRICS_SERVICE.lock().await;
        svc.inner_add_custom_event(category, action, label, custom_dimensions, Some(self)).await
    }

    pub async fn add_timing_event(
        &mut self,
        category: Option<&str>,
        time: String,
        variable: Option<&str>,
        label: Option<&str>,
        custom_dimensions: BTreeMap<&str, String>,
    ) -> Result<()> {
        if self.full_batch() {
            bail!(MAX_ENTRIES_EXCEEDED);
        }
        let svc = METRICS_SERVICE.lock().await;
        svc.inner_add_timing_event(category, time, variable, label, custom_dimensions, Some(self))
            .await
    }

    pub async fn add_crash_event(&mut self, description: &str, fatal: Option<&bool>) -> Result<()> {
        if self.full_batch() {
            bail!(MAX_ENTRIES_EXCEEDED);
        }
        let svc = METRICS_SERVICE.lock().await;
        svc.inner_add_crash_event(description, fatal, Some(self)).await
    }

    pub async fn send_events(&self) -> Result<()> {
        // TODO this guard is not necessary because the way it is constructed prevents the erroneous path
        let svc = METRICS_SERVICE.lock().await;
        match svc.init_state {
            MetricsServiceInitStatus::INITIALIZED => {
                svc.inner_send_events(self.event_strings_one_per_line(), true).await
            }
            MetricsServiceInitStatus::UNINITIALIZED => {
                log::error!("send_events called on uninitialized METRICS_SERVICE");
                bail!(INIT_ERROR)
            }
        }
    }

    pub(crate) fn add_event_string(&mut self, post_string: String) {
        self.events.push(post_string);
    }

    fn full_batch(&self) -> bool {
        self.events.len() == 20
    }

    fn event_strings_one_per_line(&self) -> String {
        self.events.join("\n")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_too_many_events(_r: Result<()>) -> Result<()> {
        match _r {
            Err(_e) => {
                assert_eq!(MAX_ENTRIES_EXCEEDED, _e.to_string());
                return Ok(());
            }
            Ok(()) => bail!("Should have bailed with MAX_ENTRIES_EXCEEDED error"),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_timing_event_count_over_limit_bails() -> Result<()> {
        let mut batch = MetricsEventBatch { events: vec!["_".to_string(); 20] };
        let _r =
            batch.add_timing_event(None, "1000".to_string(), None, None, BTreeMap::new()).await;
        assert_too_many_events(_r)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_crash_event_count_over_limit_bails() -> Result<()> {
        let mut batch = MetricsEventBatch { events: vec!["_".to_string(); 20] };
        let _r = batch.add_crash_event("Oops", None).await;
        assert_too_many_events(_r)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_launch_event_count_over_limit_bails() -> Result<()> {
        let mut batch = MetricsEventBatch { events: vec!["_".to_string(); 20] };
        let _r = batch.add_launch_event(Some("foo")).await;
        assert_too_many_events(_r)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_custom_event_count_over_limit_bails() -> Result<()> {
        let mut batch = MetricsEventBatch { events: vec!["_".to_string(); 20] };
        let _r = batch.add_custom_event(None, None, None, BTreeMap::new()).await;
        assert_too_many_events(_r)
    }

    #[test]
    fn add_event_string_adds_string() {
        let mut batch = MetricsEventBatch { events: vec![] };
        batch.add_event_string("string 1".to_string());
        assert_eq!("string 1".to_string(), batch.event_strings_one_per_line());
    }

    #[test]
    fn add_event_string_adds_both_strings() {
        let mut batch = MetricsEventBatch { events: vec![] };
        batch.add_event_string("string 1".to_string());
        batch.add_event_string("string 2".to_string());
        assert_eq!("string 1\nstring 2".to_string(), batch.event_strings_one_per_line());
    }
}

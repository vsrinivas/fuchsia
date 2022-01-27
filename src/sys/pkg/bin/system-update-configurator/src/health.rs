// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bridge::{Bridge, BridgeError, OptOutPreference};
use async_trait::async_trait;
use fuchsia_inspect::health::Reporter;
use std::cell::RefCell;

pub struct HealthStatus {
    node: RefCell<fuchsia_inspect::health::Node>,
}

impl HealthStatus {
    pub fn new(root: &fuchsia_inspect::Node) -> Self {
        let mut node = fuchsia_inspect::health::Node::new(root);
        node.set_ok();
        Self { node: RefCell::new(node) }
    }

    fn set_bridge_error(&self, err: Option<&BridgeError>) {
        let mut node = self.node.borrow_mut();

        match err {
            Some(err) => node.set_unhealthy(&format!("Error interacting with storage: {err:#}")),
            None => node.set_ok(),
        }
    }

    pub fn wrap_bridge<I>(self, bridge: I) -> BridgeWithHealthStatus<I> {
        BridgeWithHealthStatus { status: self, inner: bridge }
    }
}

pub struct BridgeWithHealthStatus<I> {
    status: HealthStatus,
    inner: I,
}

#[async_trait(?Send)]
impl<I> Bridge for BridgeWithHealthStatus<I>
where
    I: Bridge,
{
    async fn get_opt_out(&self) -> Result<OptOutPreference, BridgeError> {
        let res = self.inner.get_opt_out().await;
        self.status.set_bridge_error(res.as_ref().err());
        res
    }

    async fn set_opt_out(&mut self, value: OptOutPreference) -> Result<(), BridgeError> {
        let res = self.inner.set_opt_out(value).await;
        self.status.set_bridge_error(res.as_ref().err());
        res
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bridge;
    use fuchsia_inspect::testing::{assert_data_tree, AnyProperty};
    use fuchsia_inspect::Inspector;

    fn verify_healthy(inspector: &Inspector) {
        assert_data_tree!(
            inspector,
            root: {
                "fuchsia.inspect.Health": {
                    "start_timestamp_nanos": AnyProperty,
                    "status": "OK",
                }
            }
        );
    }

    fn verify_unhealthy(inspector: &Inspector) {
        assert_data_tree!(
            inspector,
            root: {
                "fuchsia.inspect.Health": {
                    "start_timestamp_nanos": AnyProperty,
                    "status": "UNHEALTHY",
                    "message": AnyProperty,
                }
            }
        );
    }

    #[fuchsia::test]
    async fn starts_healthy() {
        let inspector = Inspector::new();

        let storage = bridge::testing::Fake::new(OptOutPreference::AllowAllUpdates);
        let _storage = HealthStatus::new(inspector.root()).wrap_bridge(storage);

        verify_healthy(&inspector);
    }

    #[fuchsia::test]
    async fn updates_health_on_storage_error() {
        let inspector = Inspector::new();

        let (storage, fail_requests) =
            bridge::testing::Fake::new_with_error_toggle(OptOutPreference::AllowAllUpdates);
        let storage = HealthStatus::new(inspector.root()).wrap_bridge(storage);

        verify_healthy(&inspector);

        fail_requests.set(true);
        storage.get_opt_out().await.unwrap_err();

        verify_unhealthy(&inspector);
    }

    #[fuchsia::test]
    async fn updates_health_on_storage_success() {
        let inspector = Inspector::new();

        let (storage, fail_requests) =
            bridge::testing::Fake::new_with_error_toggle(OptOutPreference::AllowAllUpdates);
        let storage = HealthStatus::new(inspector.root()).wrap_bridge(storage);

        fail_requests.set(true);
        storage.get_opt_out().await.unwrap_err();

        fail_requests.set(false);
        storage.get_opt_out().await.unwrap();

        verify_healthy(&inspector);
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _, Result};
use async_lock::Mutex;
use async_trait::async_trait;
use ffx_config::{self, api::ConfigError, ConfigLevel};
use serde_json::{json, Map, Value};
use std::sync::atomic::{AtomicUsize, Ordering};

#[cfg(test)]
pub(crate) const MANUAL_TARGETS: &'static str = "targets.manual";
#[cfg(not(test))]
const MANUAL_TARGETS: &'static str = "targets.manual";

#[async_trait(?Send)]
pub trait ManualTargets: Sync {
    async fn storage_set(&self, targets: Value) -> Result<()>;
    async fn storage_get(&self) -> Result<Value>;

    async fn get(&self) -> Result<Value> {
        self.storage_get().await
    }

    async fn get_or_default(&self) -> Map<String, Value> {
        self.get().await.unwrap_or(Value::default()).as_object().unwrap_or(&Map::new()).clone()
    }

    async fn add(&self, target: String, expiry: Option<u64>) -> Result<()> {
        let mut targets = self.get_or_default().await;
        targets.insert(target, json!(expiry));
        self.storage_set(targets.into()).await
    }

    async fn remove(&self, target: String) -> Result<()> {
        let mut targets = self.get_or_default().await;
        targets.remove(&target);
        self.storage_set(targets.into()).await
    }
}

#[derive(Default)]
pub struct Config();

#[async_trait(?Send)]
impl ManualTargets for Config {
    async fn storage_get(&self) -> Result<Value> {
        ffx_config::get((MANUAL_TARGETS, ConfigLevel::User)).await.context("manual_targets::get")
    }

    async fn storage_set(&self, targets: Value) -> Result<()> {
        ffx_config::set((MANUAL_TARGETS, ConfigLevel::User), targets.into()).await
    }
}

#[derive(Default)]
pub struct Mock {
    targets: Mutex<Option<Value>>,
    set_count: AtomicUsize,
}

#[async_trait(?Send)]
impl ManualTargets for Mock {
    async fn storage_get(&self) -> Result<Value> {
        self.targets
            .lock()
            .await
            .clone()
            .map(|t| Ok(t))
            .unwrap_or(Err(anyhow::Error::new(ConfigError::new(anyhow!("value missing")))))
    }

    async fn storage_set(&self, targets: Value) -> Result<()> {
        let _ = self
            .set_count
            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |_| {
                Some(targets.as_object().unwrap().len())
            })
            .expect("Couldn't update target count for Mock.");
        self.targets.lock().await.replace(targets);
        Ok(())
    }
}

impl Mock {
    #[cfg(test)]
    pub fn new(targets: Map<String, Value>) -> Self {
        Self { targets: Mutex::new(Some(Value::from(targets))), ..Self::default() }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use serial_test::serial;

    mod real_impl {
        use super::*;
        use serde_json::json;
        use serial_test::serial;

        #[fuchsia_async::run_singlethreaded(test)]
        #[serial]
        async fn test_get_manual_targets() {
            ffx_config::test_init().unwrap();

            ffx_config::set(
                (MANUAL_TARGETS, ConfigLevel::User),
                json!({"127.0.0.1:8022": 0, "127.0.0.1:8023": 12345}),
            )
            .await
            .unwrap();

            let mt = Config::default();
            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
            assert!(targets.contains_key("127.0.0.1:8023"));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        #[serial]
        async fn test_add_manual_target() {
            ffx_config::test_init().unwrap();

            let mt = Config::default();
            mt.add("127.0.0.1:8022".to_string(), None).await.unwrap();
            mt.add("127.0.0.1:8023".to_string(), Some(12345)).await.unwrap();
            // duplicate additions are ignored
            mt.add("127.0.0.1:8022".to_string(), None).await.unwrap();
            mt.add("127.0.0.1:8023".to_string(), Some(12345)).await.unwrap();

            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
            assert!(targets.contains_key("127.0.0.1:8023"));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        #[serial]
        async fn test_remove_manual_target() {
            ffx_config::test_init().unwrap();

            ffx_config::set(
                (MANUAL_TARGETS, ConfigLevel::User),
                json!({"127.0.0.1:8022": 0, "127.0.0.1:8023": 12345}),
            )
            .await
            .unwrap();

            let mt = Config::default();
            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
            assert!(targets.contains_key("127.0.0.1:8023"));

            mt.remove("127.0.0.1:8022".to_string()).await.unwrap();
            mt.remove("127.0.0.1:8023".to_string()).await.unwrap();

            let targets = mt.get_or_default().await;
            assert_eq!(targets, Map::<String, Value>::new());
        }
    }

    mod mock_impl {
        use super::*;

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_new() {
            let mut map = Map::new();
            map.insert("127.0.0.1:8022".to_string(), json!(0));
            let mt = Mock::new(map);
            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_default() {
            let mt = Mock::default();
            assert_eq!(mt.get_or_default().await, Map::<String, Value>::new());
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_get_manual_targets() {
            let mut map = Map::new();
            map.insert("127.0.0.1:8022".to_string(), json!(0));
            map.insert("127.0.0.1:8023".to_string(), json!(0));
            let mt = Mock::new(map);
            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
            assert!(targets.contains_key("127.0.0.1:8023"));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_add_manual_target() {
            let mt = Mock::default();
            mt.add("127.0.0.1:8022".to_string(), None).await.unwrap();
            mt.add("127.0.0.1:8023".to_string(), Some(12345)).await.unwrap();
            // duplicate additions are ignored
            mt.add("127.0.0.1:8022".to_string(), None).await.unwrap();
            mt.add("127.0.0.1:8023".to_string(), Some(12345)).await.unwrap();

            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
            assert!(targets.contains_key("127.0.0.1:8023"));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_remove_manual_target() {
            let mut map = Map::new();
            map.insert("127.0.0.1:8022".to_string(), json!(0));
            map.insert("127.0.0.1:8023".to_string(), json!(12345));
            let mt = Mock::new(map);
            let value = mt.get().await.unwrap();
            let targets = value.as_object().unwrap();
            assert!(targets.contains_key("127.0.0.1:8022"));
            assert!(targets.contains_key("127.0.0.1:8023"));

            mt.remove("127.0.0.1:8022".to_string()).await.unwrap();
            mt.remove("127.0.0.1:8023".to_string()).await.unwrap();

            let targets = mt.get_or_default().await;
            assert!(targets.is_empty());
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[serial]
    async fn test_repeated_adds_do_not_rewrite_storage() {
        let mt = Mock::new(Map::new());
        mt.add("127.0.0.1:8022".to_string(), None).await.unwrap();
        assert_eq!(mt.set_count.load(Ordering::SeqCst), 1);
        mt.add("127.0.0.1:8022".to_string(), None).await.unwrap();
        assert_eq!(mt.set_count.load(Ordering::SeqCst), 1);
        mt.add("127.0.0.1:8023".to_string(), Some(12345)).await.unwrap();
        assert_eq!(mt.set_count.load(Ordering::SeqCst), 2);
        mt.add("127.0.0.1:8023".to_string(), Some(12345)).await.unwrap();
        assert_eq!(mt.set_count.load(Ordering::SeqCst), 2);
    }
}

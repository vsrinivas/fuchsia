// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _, Result};
use async_lock::Mutex;
use async_trait::async_trait;
use ffx_config::{self, api::ConfigError, ConfigLevel};
use std::collections::HashSet;
use std::iter::FromIterator;

#[cfg(test)]
pub(crate) const MANUAL_TARGETS: &'static str = "targets.manual";
#[cfg(not(test))]
const MANUAL_TARGETS: &'static str = "targets.manual";

#[async_trait(?Send)]
pub trait ManualTargets: Sync {
    async fn storage_set(&self, targets: Vec<String>) -> Result<()>;
    async fn storage_get(&self) -> Result<Vec<String>>;

    async fn get(&self) -> Result<Vec<String>> {
        self.storage_get().await
    }

    async fn get_or_default(&self) -> Vec<String> {
        self.get().await.unwrap_or(Vec::new())
    }

    async fn add(&self, target: String) -> Result<()> {
        let mut targets = self.get_or_default().await;
        if !targets.contains(&target) {
            targets.push(target)
        }
        self.storage_set(targets.into()).await
    }

    async fn remove(&self, target: String) -> Result<()> {
        let targets = self.get_or_default().await;
        let mut set = HashSet::<String>::from_iter(targets.into_iter());
        set.remove(&target);
        let targets = set.into_iter().collect::<Vec<String>>();
        self.storage_set(targets.into()).await
    }
}

#[derive(Default)]
pub struct Config();

#[async_trait(?Send)]
impl ManualTargets for Config {
    async fn storage_get(&self) -> Result<Vec<String>> {
        ffx_config::get((MANUAL_TARGETS, ConfigLevel::User)).await.context("manual_targets::get")
    }

    async fn storage_set(&self, targets: Vec<String>) -> Result<()> {
        ffx_config::set((MANUAL_TARGETS, ConfigLevel::User), targets.into()).await
    }
}

#[derive(Default)]
pub struct Mock {
    targets: Mutex<Option<Vec<String>>>,
}

#[async_trait(?Send)]
impl ManualTargets for Mock {
    async fn storage_get(&self) -> Result<Vec<String>> {
        self.targets
            .lock()
            .await
            .clone()
            .map(|t| Ok(t))
            .unwrap_or(Err(anyhow::Error::new(ConfigError::new(anyhow!("value missing")))))
    }

    async fn storage_set(&self, targets: Vec<String>) -> Result<()> {
        self.targets.lock().await.replace(targets);
        Ok(())
    }
}

impl Mock {
    #[cfg(test)]
    pub fn new(targets: Vec<String>) -> Self {
        Self { targets: Mutex::new(Some(targets)) }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    mod real_impl {
        use super::*;
        use serial_test::serial;

        #[fuchsia_async::run_singlethreaded(test)]
        #[serial]
        async fn test_get_manual_targets() {
            ffx_config::init(&[], None, None).unwrap();

            ffx_config::set(
                (MANUAL_TARGETS, ConfigLevel::User),
                vec!["127.0.0.1:8022".to_string(), "127.0.0.1:8023".to_string()].into(),
            )
            .await
            .unwrap();

            let mt = Config::default();
            let targets = mt.get().await.unwrap();
            assert!(targets.contains(&"127.0.0.1:8022".to_string()));
            assert!(targets.contains(&"127.0.0.1:8023".to_string()));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        #[serial]
        async fn test_add_manual_target() {
            ffx_config::init(&[], None, None).unwrap();

            let mt = Config::default();
            mt.add("127.0.0.1:8022".to_string()).await.unwrap();
            // duplicate additions are ignored
            mt.add("127.0.0.1:8022".to_string()).await.unwrap();
            mt.add("127.0.0.1:8023".to_string()).await.unwrap();

            let targets = mt.get().await.unwrap();
            assert!(targets.contains(&"127.0.0.1:8022".to_string()));
            assert!(targets.contains(&"127.0.0.1:8023".to_string()));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        #[serial]
        async fn test_remove_manual_target() {
            ffx_config::init(&[], None, None).unwrap();

            ffx_config::set(
                (MANUAL_TARGETS, ConfigLevel::User),
                vec!["127.0.0.1:8022".to_string()].into(),
            )
            .await
            .unwrap();

            let mt = Config::default();
            let targets = mt.get().await.unwrap();
            assert_eq!(targets, vec!["127.0.0.1:8022"]);

            mt.remove("127.0.0.1:8022".to_string()).await.unwrap();

            let targets = mt.get_or_default().await;
            assert_eq!(targets, Vec::<String>::new());
        }
    }

    mod mock_impl {
        use super::*;

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_new() {
            let mt = Mock::new(vec!["127.0.0.1:8022".to_string()]);
            assert_eq!(mt.get().await.unwrap(), vec!["127.0.0.1:8022".to_string()]);
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_default() {
            let mt = Mock::default();
            assert_eq!(mt.get_or_default().await, Vec::<String>::new());
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_get_manual_targets() {
            let mt = Mock::new(vec!["127.0.0.1:8022".to_string(), "127.0.0.1:8023".to_string()]);
            let targets = mt.get().await.unwrap();
            assert!(targets.contains(&"127.0.0.1:8022".to_string()));
            assert!(targets.contains(&"127.0.0.1:8023".to_string()));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_add_manual_target() {
            let mt = Mock::default();
            mt.add("127.0.0.1:8022".to_string()).await.unwrap();
            // duplicate additions are ignored
            mt.add("127.0.0.1:8022".to_string()).await.unwrap();
            mt.add("127.0.0.1:8023".to_string()).await.unwrap();

            let targets = mt.get().await.unwrap();
            assert!(targets.contains(&"127.0.0.1:8022".to_string()));
            assert!(targets.contains(&"127.0.0.1:8023".to_string()));
        }

        #[fuchsia_async::run_singlethreaded(test)]
        async fn test_remove_manual_target() {
            let mt = Mock::new(vec!["127.0.0.1:8022".to_string()]);
            let targets = mt.get().await.unwrap();
            assert_eq!(targets, vec!["127.0.0.1:8022"]);

            mt.remove("127.0.0.1:8022".to_string()).await.unwrap();

            let targets = mt.get_or_default().await;
            assert_eq!(targets, Vec::<String>::new());
        }
    }
}

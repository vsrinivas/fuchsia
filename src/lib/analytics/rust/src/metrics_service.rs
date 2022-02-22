// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fuchsia_hyper::{new_https_client, HttpsClient},
    futures::lock::Mutex,
    hyper::body::HttpBody,
    hyper::{Body, Method, Request},
    lazy_static::lazy_static,
    std::collections::BTreeMap,
    std::sync::Arc,
};

use crate::ga_event::*;
use crate::metrics_state::*;
use crate::notice::{BRIEF_NOTICE, FULL_NOTICE};
use crate::MetricsEventBatch;

#[cfg(test)]
const GA_URL: &str = "https://www.google-analytics.com/debug/collect";
#[cfg(not(test))]
const GA_URL: &str = "https://www.google-analytics.com/collect";
const GA_BATCH_URL: &str = "https://www.google-analytics.com/batch";

lazy_static! {
    pub(crate) static ref METRICS_SERVICE: Arc<Mutex<MetricsService>> = Arc::new(Mutex::new(MetricsService::default()));
    // Note: lazy_static does not run any deconstructors.
}

/// The implementation of the metrics public api.
/// By default a new instance is uninitialized until
/// the state has been added.
pub(crate) struct MetricsService {
    pub(crate) init_state: MetricsServiceInitStatus,
    state: MetricsState,
    client: HttpsClient,
}

#[derive(Debug, PartialEq)]
pub(crate) enum MetricsServiceInitStatus {
    UNINITIALIZED,
    INITIALIZED,
}

impl MetricsService {
    pub(crate) fn inner_init(&mut self, state: MetricsState) {
        self.init_state = MetricsServiceInitStatus::INITIALIZED;
        self.state = state;
    }

    pub(crate) fn inner_get_notice(&self) -> Option<String> {
        match self.state.status {
            MetricsStatus::NewUser => {
                let formatted_for_app: String =
                    FULL_NOTICE.replace("{app_name}", &self.state.app_name);
                Some(formatted_for_app.to_owned())
            }
            MetricsStatus::NewToTool => {
                let formatted_for_app: String =
                    BRIEF_NOTICE.replace("{app_name}", &self.state.app_name);
                Some(formatted_for_app.to_owned())
            }
            _ => None,
        }
    }

    pub(crate) fn inner_set_opt_in_status(&mut self, enabled: bool) -> Result<()> {
        self.state.set_opt_in_status(enabled)
    }

    pub(crate) fn inner_is_opted_in(&self) -> bool {
        self.state.is_opted_in()
    }

    // disable analytics for this invocation only
    // this does not affect the global analytics state
    pub fn inner_opt_out_for_this_invocation(&mut self) -> Result<()> {
        self.state.opt_out_for_this_invocation()
    }

    fn uuid_as_str(&self) -> String {
        self.state.uuid.map_or("No uuid".to_string(), |u| u.to_string())
    }

    pub(crate) async fn inner_add_launch_event(
        &self,
        args: Option<&str>,
        batch_collector: Option<&mut MetricsEventBatch>,
    ) -> Result<()> {
        // TODO(fxb/71580): extract param for category when requirements are clear.
        // For tools with subcommands, e.g. ffx, could be subcommands for better analysis
        self.inner_add_custom_event(None, args, args, BTreeMap::new(), batch_collector).await
    }

    pub(crate) async fn inner_add_custom_event(
        &self,
        category: Option<&str>,
        action: Option<&str>,
        label: Option<&str>,
        custom_dimensions: BTreeMap<&str, String>,
        batch_collector: Option<&mut MetricsEventBatch>,
    ) -> Result<()> {
        if self.inner_is_opted_in() {
            let body = make_body_with_hash(
                &self.state.app_name,
                Some(&self.state.build_version),
                &self.state.ga_product_code,
                category,
                action,
                label,
                custom_dimensions,
                self.uuid_as_str(),
            );
            match batch_collector {
                None => {
                    let req = Request::builder()
                        .method(Method::POST)
                        .uri(GA_URL)
                        .body(Body::from(body))?;
                    let res = self.client.request(req).await;
                    match res {
                        Ok(res) => log::info!("Analytics response: {}", res.status()),
                        Err(e) => log::debug!("Error posting analytics: {}", e),
                    }
                    Ok(())
                }
                Some(bc) => {
                    bc.add_event_string(body);
                    Ok(())
                }
            }
        } else {
            Ok(())
        }
    }

    // TODO(fxb/70502): Add command crash
    // fx exception in subcommand
    // "t=event" \
    // "ec=fx_exception" \
    // "ea=${subcommand}" \
    // "el=${args}" \
    // "cd1=${exit_status}" \
    // )
    pub(crate) async fn inner_add_crash_event(
        &self,
        description: &str,
        fatal: Option<&bool>,
        batch_collector: Option<&mut MetricsEventBatch>,
    ) -> Result<()> {
        if self.inner_is_opted_in() {
            let body = make_crash_body_with_hash(
                &self.state.app_name,
                Some(&self.state.build_version),
                &self.state.ga_product_code,
                description,
                fatal,
                BTreeMap::new(),
                self.uuid_as_str(),
            );
            match batch_collector {
                None => {
                    let req = Request::builder()
                        .method(Method::POST)
                        .uri(GA_URL)
                        .body(Body::from(body))?;
                    let res = self.client.request(req).await;
                    match res {
                        Ok(res) => log::info!("Analytics response: {}", res.status()),
                        Err(e) => log::debug!("Error posting analytics: {}", e),
                    }
                    Ok(())
                }
                Some(bc) => {
                    bc.add_event_string(body);
                    Ok(())
                }
            }
        } else {
            Ok(())
        }
    }

    /// Records a timing event from the app.
    /// Returns an error if init has not been called.
    pub(crate) async fn inner_add_timing_event(
        &self,
        category: Option<&str>,
        time: String,
        variable: Option<&str>,
        label: Option<&str>,
        custom_dimensions: BTreeMap<&str, String>,
        batch_collector: Option<&mut MetricsEventBatch>,
    ) -> Result<()> {
        if self.inner_is_opted_in() {
            let body = make_timing_body_with_hash(
                &self.state.app_name,
                Some(&self.state.build_version),
                &self.state.ga_product_code,
                category,
                time,
                variable,
                label,
                custom_dimensions,
                self.uuid_as_str(),
            );
            match batch_collector {
                None => {
                    let req = Request::builder()
                        .method(Method::POST)
                        .uri(GA_URL)
                        .body(Body::from(body))?;
                    let res = self.client.request(req).await;
                    match res {
                        Ok(res) => log::info!("Analytics response: {}", res.status()),
                        Err(e) => log::debug!("Error posting analytics: {}", e),
                    }
                    Ok(())
                }
                Some(bc) => {
                    bc.add_event_string(body);
                    Ok(())
                }
            }
        } else {
            Ok(())
        }
    }

    pub(crate) async fn inner_send_events(&self, body: String, batch: bool) -> Result<()> {
        if self.inner_is_opted_in() {
            let url = match batch {
                true => GA_BATCH_URL,
                false => GA_URL,
            };

            log::debug!("POSTING ANALYTICS: url: {}, \nBODY: {}", &url, &body);

            let req = Request::builder().method(Method::POST).uri(url).body(Body::from(body))?;
            let res = self.client.request(req).await;
            match res {
                Ok(mut res) => {
                    log::info!("Analytics response: {}", res.status());
                    while let Some(chunk) = res.body_mut().data().await {
                        log::debug!("{:?}", &chunk?);
                    }
                    //let result = String::from_utf8(bytes.into_iter().collect()).expect("");
                }
                Err(e) => log::debug!("Error posting analytics: {}", e),
            }
            Ok(())
        } else {
            Ok(())
        }
    }
}

impl Default for MetricsService {
    fn default() -> Self {
        Self {
            init_state: MetricsServiceInitStatus::UNINITIALIZED,
            state: MetricsState::default(),
            client: new_https_client(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metrics_state::write_app_status;
    use crate::metrics_state::write_opt_in_status;
    use crate::metrics_state::UNKNOWN_PROPERTY_ID;
    use std::path::PathBuf;
    use tempfile::tempdir;

    const APP_NAME: &str = "my cool app";
    const BUILD_VERSION: &str = "12/09/20 00:00:00";
    // const LAUNCH_ARGS: &str = "config analytics enable";

    fn test_metrics_svc(
        app_support_dir_path: &PathBuf,
        app_name: String,
        build_version: String,
        ga_product_code: String,
        disabled: bool,
    ) -> MetricsService {
        MetricsService {
            init_state: MetricsServiceInitStatus::INITIALIZED,
            state: MetricsState::from_config(
                app_support_dir_path,
                app_name,
                build_version,
                ga_product_code,
                disabled,
            ),
            client: new_https_client(),
        }
    }

    #[test]
    fn new_user_of_any_tool() -> Result<()> {
        let dir = create_tmp_metrics_dir()?;
        let ms = test_metrics_svc(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_eq!(ms.inner_get_notice(), Some(FULL_NOTICE.replace("{app_name}", APP_NAME)));

        drop(dir);
        Ok(())
    }

    #[test]
    fn existing_user_first_use_of_this_tool() -> Result<()> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, true)?;

        let ms = test_metrics_svc(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_eq!(ms.state.status, MetricsStatus::NewToTool);
        assert_eq!(ms.inner_get_notice(), Some(BRIEF_NOTICE.replace("{app_name}", APP_NAME)));
        drop(dir);
        Ok(())
    }

    #[test]
    fn existing_user_of_this_tool_opted_in() -> Result<()> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, true)?;
        write_app_status(&dir, &APP_NAME, true)?;
        let ms = test_metrics_svc(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_eq!(ms.inner_get_notice(), None);
        drop(dir);
        Ok(())
    }

    #[test]
    fn existing_user_of_this_tool_opted_out() -> Result<()> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, false)?;
        write_app_status(&dir, &APP_NAME, true)?;
        let ms = test_metrics_svc(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_eq!(ms.inner_get_notice(), None);

        drop(dir);
        Ok(())
    }

    #[test]
    fn with_disable_env_var_set() -> Result<()> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, true)?;
        write_app_status(&dir, &APP_NAME, true)?;

        let ms = test_metrics_svc(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            true,
        );

        assert_eq!(ms.inner_get_notice(), None);

        drop(dir);
        Ok(())
    }

    #[test]
    fn opt_out_for_this_invocation() -> Result<()> {
        let dir = create_tmp_metrics_dir()?;
        let mut ms = test_metrics_svc(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_eq!(ms.state.status, MetricsStatus::NewUser);
        let _res = ms.inner_opt_out_for_this_invocation().unwrap();
        assert_eq!(ms.state.status, MetricsStatus::OptedOut);

        drop(dir);
        Ok(())
    }

    pub fn create_tmp_metrics_dir() -> Result<PathBuf> {
        let tmp_dir = tempdir()?;
        let dir_obj = tmp_dir.path().join("fuchsia_metrics");
        let dir = dir_obj.as_path();
        Ok(dir.to_owned())
    }
}

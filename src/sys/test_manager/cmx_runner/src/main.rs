// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys as fsysv1, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    legacy_component_lib::LegacyComponent,
    rand::{self, Rng},
    std::convert::TryInto,
    std::sync::Arc,
    thiserror::Error,
    tracing::{info, warn},
    vfs::execution_scope::ExecutionScope,
};

const LEGACY_URL_KEY: &'static str = "legacy_url";

#[fuchsia::component]
fn main() -> Result<(), anyhow::Error> {
    info!("started");
    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            async move { start_runner(stream).await.expect("failed to start runner.") },
        )
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

#[derive(Debug, Error)]
pub enum RunnerError {
    #[error("Cannot read fuchsia.component.runner.ComponentRunner request: {:?}", _0)]
    RequestRead(fidl::Error),
}

#[derive(Debug, Error, PartialEq, Eq)]
enum StartupInfoError {
    #[error("Cannot get resolved url from legacy component manifest")]
    NoResolvedUrl,

    #[error("Cannot get legacy component url for {} as it doesn't contain program entry", _0)]
    NoProgramEntry(String),

    #[error(
        "Cannot get legacy component url for {} as it doesn't contain {} entry",
        _0,
        LEGACY_URL_KEY
    )]
    NoLegacyUrl(String),

    #[error("Cannot get legacy component url for {} as {} contains no value", _0, LEGACY_URL_KEY)]
    LegacyUrlNoValue(String),

    #[error(
        "Cannot get legacy component url for {} as {} contains no url or contains args",
        _0,
        LEGACY_URL_KEY
    )]
    InvalidLegacyUrl(String),
}

/// Returns (resolved_url, legacy_url) from startup info.
fn get_urls(
    start_info: &fcrunner::ComponentStartInfo,
) -> Result<(String, String), StartupInfoError> {
    let resolved_url = start_info.resolved_url.as_ref().ok_or(StartupInfoError::NoResolvedUrl)?;

    let program =
        start_info.program.as_ref().ok_or(StartupInfoError::NoProgramEntry(resolved_url.into()))?;

    let entries =
        program.entries.as_ref().ok_or(StartupInfoError::NoProgramEntry(resolved_url.into()))?;

    let legacy_url_entry = entries
        .into_iter()
        .find(|e| e.key == LEGACY_URL_KEY)
        .ok_or(StartupInfoError::NoLegacyUrl(resolved_url.into()))?;

    let legacy_url = legacy_url_entry
        .value
        .as_ref()
        .ok_or(StartupInfoError::LegacyUrlNoValue(resolved_url.into()))?
        .as_ref();
    match legacy_url {
        fdata::DictionaryValue::Str(url) => Ok((resolved_url.clone(), url.clone())),
        _ => {
            warn!(
                "received invalid legacy url type. Expected string, but received: {:?}",
                legacy_url
            );
            return Err(StartupInfoError::InvalidLegacyUrl(resolved_url.into()));
        }
    }
}

async fn start_runner(
    mut stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), RunnerError> {
    let env_proxy = Arc::new(
        connect_to_protocol::<fsysv1::EnvironmentMarker>()
            .expect("cannot connect to fuchsia.sys.Environment"),
    );
    while let Some(event) = stream.try_next().await.map_err(RunnerError::RequestRead)? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let (url, legacy_url) = match get_urls(&start_info) {
                    Ok(u) => u,
                    Err(e) => {
                        warn!("{}", e);
                        let _: Result<(), fidl::Error> =
                            controller.close_with_epitaph(zx::Status::from_raw(
                                fcomponent::Error::InvalidArguments
                                    .into_primitive()
                                    .try_into()
                                    .unwrap(),
                            ));
                        continue;
                    }
                };

                let parent_env = env_proxy.clone();
                // TODO(anmittal): Figure out if we can attach this task to any object to manage its
                // lifetime.
                fasync::Task::spawn(async move {
                    let execution_scope = ExecutionScope::new();
                    let id: u64 = rand::thread_rng().gen();
                    let realm_label = format!("legacy_component-{}", id);
                    info!("launch '{}' for '{}' in env '{}'", legacy_url, url, realm_label);
                    let legacy_component = match LegacyComponent::run(
                        legacy_url,
                        start_info,
                        parent_env,
                        realm_label,
                        execution_scope.clone(),
                    )
                    .await
                    {
                        Ok(component) => component,
                        Err(e) => {
                            warn!("Error running {}: {:?}", url, e);
                            // we don't care about the error if the client is gone.
                            let _: Result<(), fidl::Error> =
                                controller.close_with_epitaph(zx::Status::from_raw(
                                    fcomponent::Error::InstanceCannotStart
                                        .into_primitive()
                                        .try_into()
                                        .unwrap(),
                                ));
                            return;
                        }
                    };
                    let stream = match controller.into_stream() {
                        Ok(s) => s,
                        Err(e) => {
                            warn!("Error serving controller for {}: {:?}", url, e);
                            return;
                        }
                    };
                    if let Err(e) = legacy_component.serve_controller(stream, execution_scope).await
                    {
                        warn!("Cannot serve controller for {}: {}", url, e);
                    }
                })
                .detach();
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn get_urls_test() {
        let mut start_info = fcrunner::ComponentStartInfo::EMPTY;
        assert_eq!(StartupInfoError::NoResolvedUrl, get_urls(&start_info).err().unwrap());

        let url = "fuchsia-pkg://fuchsia.com/package#meta/component.cm".to_string();
        start_info.resolved_url = url.clone().into();
        assert_eq!(
            StartupInfoError::NoProgramEntry(url.clone()),
            get_urls(&start_info).err().unwrap()
        );

        start_info.program = Some(fdata::Dictionary::EMPTY);
        assert_eq!(
            StartupInfoError::NoProgramEntry(url.clone()),
            get_urls(&start_info).err().unwrap()
        );

        start_info.program =
            Some(fdata::Dictionary { entries: Some(vec![]), ..fdata::Dictionary::EMPTY });
        assert_eq!(
            StartupInfoError::NoLegacyUrl(url.clone()),
            get_urls(&start_info).err().unwrap()
        );

        start_info.program = Some(fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: "some_key".into(),
                value: Some(Box::new(fdata::DictionaryValue::Str("some_val".into()))),
            }]),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(
            StartupInfoError::NoLegacyUrl(url.clone()),
            get_urls(&start_info).err().unwrap()
        );

        start_info.program = Some(fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry { key: LEGACY_URL_KEY.into(), value: None }]),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(
            StartupInfoError::LegacyUrlNoValue(url.clone()),
            get_urls(&start_info).err().unwrap()
        );

        start_info.program = Some(fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: LEGACY_URL_KEY.into(),
                value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![
                    "fuchsia-pkg://fuchsia.com/package#meta/component.cmx".into(),
                ]))),
            }]),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(
            StartupInfoError::InvalidLegacyUrl(url.clone()),
            get_urls(&start_info).err().unwrap()
        );

        start_info.program = Some(fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: LEGACY_URL_KEY.into(),
                value: Some(Box::new(fdata::DictionaryValue::Str(
                    "fuchsia-pkg://fuchsia.com/package#meta/component.cmx".into(),
                ))),
            }]),
            ..fdata::Dictionary::EMPTY
        });
        assert_eq!(
            (url, "fuchsia-pkg://fuchsia.com/package#meta/component.cmx".to_string()),
            get_urls(&start_info).unwrap()
        );
    }
}

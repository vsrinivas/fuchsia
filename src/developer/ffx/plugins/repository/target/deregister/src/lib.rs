// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_core::ffx_plugin,
    ffx_repository_target_deregister_args::DeregisterCommand,
    fidl_fuchsia_developer_bridge::{RepositoryError, RepositoryRegistryProxy},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn deregister_cmd(cmd: DeregisterCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    deregister(
        ffx_config::get("target.default").await.context("getting default target from config")?,
        cmd,
        repos,
    )
    .await
}

async fn deregister(
    target_str: Option<String>,
    cmd: DeregisterCommand,
    repos: RepositoryRegistryProxy,
) -> Result<()> {
    let repo_name = if let Some(repo_name) = cmd.repository {
        repo_name
    } else {
        if let Some(repo_name) = pkg::config::get_default_repository().await? {
            repo_name
        } else {
            ffx_bail!(
                "Either a default repository must be set, or the --repository flag must be provided.\n\
                You can set a default repository using: `ffx repository default set <name>`."
            )
        }
    };

    repos
        .deregister_target(
            &repo_name,
            target_str.as_deref(),
        )
        .await
        .context("communicating with daemon")?
        .map_err(|e| match e {
            RepositoryError::TargetCommunicationFailure=>
                anyhow!(ffx_error!("Failed to communicate with the target. Ensure that a target is running and connected with `ffx target list`")),
            _ =>
                anyhow!("failed to deregister repository: {:?}", e),

        })
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::RepositoryRegistryRequest,
        fuchsia_async as fasync,
        futures::channel::oneshot::{channel, Receiver},
    };

    const REPO_NAME: &str = "some-name";
    const TARGET_NAME: &str = "some-target";

    async fn setup_fake_server() -> (RepositoryRegistryProxy, Receiver<(String, Option<String>)>) {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::DeregisterTarget {
                repository_name,
                target_identifier,
                responder,
            } => {
                sender.take().unwrap().send((repository_name, target_identifier)).unwrap();
                responder.send(&mut Ok(())).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });
        (repos, receiver)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deregister() {
        let (repos, receiver) = setup_fake_server().await;

        deregister(
            Some(TARGET_NAME.to_string()),
            DeregisterCommand { repository: Some(REPO_NAME.to_string()) },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(got, (REPO_NAME.to_string(), Some(TARGET_NAME.to_string()),));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deregister_default_repository() {
        ffx_config::init_config_test().unwrap();

        let default_repo_name = "default-repo";
        pkg::config::set_default_repository(default_repo_name).await.unwrap();

        let (repos, receiver) = setup_fake_server().await;

        deregister(Some(TARGET_NAME.to_string()), DeregisterCommand { repository: None }, repos)
            .await
            .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(got, (default_repo_name.to_string(), Some(TARGET_NAME.to_string()),));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deregister_returns_error() {
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::DeregisterTarget {
                repository_name: _,
                target_identifier: _,
                responder,
            } => {
                responder.send(&mut Err(RepositoryError::TargetCommunicationFailure)).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        assert!(deregister(
            Some(TARGET_NAME.to_string()),
            DeregisterCommand { repository: Some(REPO_NAME.to_string()) },
            repos,
        )
        .await
        .is_err());
    }
}

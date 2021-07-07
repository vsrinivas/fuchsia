// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_config::get,
    ffx_core::ffx_plugin,
    ffx_repository_target_deregister_args::DeregisterCommand,
    fidl_fuchsia_developer_bridge::{RepositoryError, RepositoryRegistryProxy},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn deregister_cmd(cmd: DeregisterCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    deregister(
        get("target.default").await.context("getting default target from config")?,
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
    if cmd.name.is_empty() {
        ffx_bail!("error: repository name must not be empty.")
    }

    repos
        .deregister_target(
            &cmd.name,
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
            Some("target_str".to_string()),
            DeregisterCommand { name: "some_name".to_string() },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(got, ("some_name".to_string(), Some("target_str".to_string()),));
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
            Some("target_str".to_string()),
            DeregisterCommand { name: "some_name".to_string() },
            repos,
        )
        .await
        .is_err());
    }
}

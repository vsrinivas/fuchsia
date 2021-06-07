// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::ffx_error,
    ffx_config::get,
    ffx_core::ffx_plugin,
    ffx_repository_target_register_args::RegisterCommand,
    fidl_fuchsia_developer_bridge::{RepositoriesProxy, RepositoryError, RepositoryTarget},
};

#[ffx_plugin(RepositoriesProxy = "daemon::service")]
pub async fn register_cmd(cmd: RegisterCommand, repos: RepositoriesProxy) -> Result<()> {
    register(get("target.default").await.context("getting default target from config")?, cmd, repos)
        .await
}

async fn register(
    target_str: Option<String>,
    cmd: RegisterCommand,
    repos: RepositoriesProxy,
) -> Result<()> {
    repos
        .register_target(RepositoryTarget {
            repo_name: Some(cmd.name),
            target_identifier: target_str,
            aliases: Some(cmd.alias),
            ..RepositoryTarget::EMPTY
        })
        .await
        .context("communicating with daemon")?
        .map_err(|e| match e {
            RepositoryError::TargetCommunicationFailure=>
                anyhow!(ffx_error!("Failed to communicate with the target. Ensure that a target is running and connected with `ffx target list`")),
            _ =>
                anyhow!("failed to register repository: {:?}", e),

        })
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::RepositoriesRequest, fuchsia_async as fasync,
        futures::channel::oneshot::channel,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_register() {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoriesRequest::RegisterTarget { target_info, responder } => {
                sender.take().unwrap().send(target_info).unwrap();
                responder.send(&mut Ok(())).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        let aliases = vec![String::from("my_alias")];
        register(
            Some("target_str".to_string()),
            RegisterCommand { name: "some_name".to_string(), alias: aliases.clone() },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            RepositoryTarget {
                repo_name: Some("some_name".to_string()),
                target_identifier: Some("target_str".to_string()),
                aliases: Some(aliases),
                ..RepositoryTarget::EMPTY
            }
        );
    }
}

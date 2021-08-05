// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_core::ffx_plugin,
    ffx_repository_target_register_args::RegisterCommand,
    fidl_fuchsia_developer_bridge::{RepositoryError, RepositoryRegistryProxy, RepositoryTarget},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn register_cmd(cmd: RegisterCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    register(
        ffx_config::get("target.default").await.context("getting default target from config")?,
        cmd,
        repos,
    )
    .await
}

async fn register(
    target_str: Option<String>,
    cmd: RegisterCommand,
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
        .register_target(RepositoryTarget {
            repo_name: Some(repo_name),
            target_identifier: target_str,
            aliases: Some(cmd.alias),
            storage_type: cmd.storage_type,
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
        ffx_config::ConfigLevel,
        fidl_fuchsia_developer_bridge::{RepositoryRegistryRequest, RepositoryStorageType},
        fuchsia_async as fasync,
        futures::channel::oneshot::{channel, Receiver},
    };

    const REPO_NAME: &str = "some-name";
    const TARGET_NAME: &str = "some-target";

    async fn setup_fake_server() -> (RepositoryRegistryProxy, Receiver<RepositoryTarget>) {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::RegisterTarget { target_info, responder } => {
                sender.take().unwrap().send(target_info).unwrap();
                responder.send(&mut Ok(())).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });
        (repos, receiver)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register() {
        let (repos, receiver) = setup_fake_server().await;

        let aliases = vec![String::from("my-alias")];
        register(
            Some(TARGET_NAME.to_string()),
            RegisterCommand {
                repository: Some(REPO_NAME.to_string()),
                alias: aliases.clone(),
                storage_type: None,
            },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: Some(TARGET_NAME.to_string()),
                aliases: Some(aliases),
                storage_type: None,
                ..RepositoryTarget::EMPTY
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register_default_repository() {
        ffx_config::init(&[], None, None).unwrap();

        let default_repo_name = "default-repo";
        ffx_config::set(("repository.default", ConfigLevel::User), default_repo_name.into())
            .await
            .unwrap();

        let (repos, receiver) = setup_fake_server().await;

        register(
            None,
            RegisterCommand { repository: None, alias: vec![], storage_type: None },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            RepositoryTarget {
                repo_name: Some(default_repo_name.to_string()),
                target_identifier: None,
                aliases: Some(vec![]),
                storage_type: None,
                ..RepositoryTarget::EMPTY
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register_storage_type() {
        let (repos, receiver) = setup_fake_server().await;

        let aliases = vec![String::from("my-alias")];
        register(
            Some(TARGET_NAME.to_string()),
            RegisterCommand {
                repository: Some(REPO_NAME.to_string()),
                alias: aliases.clone(),
                storage_type: Some(RepositoryStorageType::Persistent),
            },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: Some(TARGET_NAME.to_string()),
                aliases: Some(aliases),
                storage_type: Some(RepositoryStorageType::Persistent),
                ..RepositoryTarget::EMPTY
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register_empty_aliases() {
        let (repos, receiver) = setup_fake_server().await;

        register(
            Some(TARGET_NAME.to_string()),
            RegisterCommand {
                repository: Some(REPO_NAME.to_string()),
                alias: vec![],
                storage_type: None,
            },
            repos,
        )
        .await
        .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: Some(TARGET_NAME.to_string()),
                aliases: Some(vec![]),
                storage_type: None,
                ..RepositoryTarget::EMPTY
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register_returns_error() {
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::RegisterTarget { target_info: _, responder } => {
                responder.send(&mut Err(RepositoryError::TargetCommunicationFailure)).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        assert!(register(
            Some(TARGET_NAME.to_string()),
            RegisterCommand {
                repository: Some(REPO_NAME.to_string()),
                alias: vec![],
                storage_type: None
            },
            repos,
        )
        .await
        .is_err());
    }
}

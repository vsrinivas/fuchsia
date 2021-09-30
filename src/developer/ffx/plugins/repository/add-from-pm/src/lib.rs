// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_add_from_pm_args::AddFromPmCommand,
    fidl_fuchsia_developer_bridge::RepositoryRegistryProxy,
    fidl_fuchsia_developer_bridge_ext::{RepositoryError, RepositorySpec},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn add_from_pm(cmd: AddFromPmCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    let full_path = cmd
        .pm_repo_path
        .canonicalize()
        .with_context(|| format!("failed to canonicalize {:?}", cmd.pm_repo_path))?;
    let repo_spec = RepositorySpec::Pm { path: full_path };

    match repos.add_repository(&cmd.repository, &mut repo_spec.into()).await? {
        Ok(()) => Ok(()),
        Err(err) => {
            let err = RepositoryError::from(err);
            ffx_bail!("Adding repository {:} failed: {}", cmd.repository, err);
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::{
            PmRepositorySpec, RepositoryRegistryRequest, RepositorySpec,
        },
        fuchsia_async as fasync,
        futures::channel::oneshot::channel,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_add_from_pm() {
        let tmp = tempfile::tempdir().unwrap();

        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::AddRepository { name, repository, responder } => {
                sender.take().unwrap().send((name, repository)).unwrap();
                responder.send(&mut Ok(())).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        add_from_pm(
            AddFromPmCommand {
                repository: "MyRepo".to_owned(),
                pm_repo_path: tmp.path().to_path_buf(),
            },
            repos,
        )
        .await
        .unwrap();

        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            (
                "MyRepo".to_owned(),
                RepositorySpec::Pm(PmRepositorySpec {
                    path: Some(tmp.path().canonicalize().unwrap().to_str().unwrap().to_string()),
                    ..PmRepositorySpec::EMPTY
                })
            )
        );
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_add_from_pm_args::AddFromPmCommand,
    fidl_fuchsia_developer_bridge::RepositoryRegistryProxy,
    fidl_fuchsia_developer_bridge_ext::{RepositoryError, RepositorySpec},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn add_from_pm(cmd: AddFromPmCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    let repo_spec = RepositorySpec::Pm { path: cmd.pm_repo_path };

    match repos.add_repository(&cmd.name, &mut repo_spec.into()).await? {
        Ok(()) => Ok(()),
        Err(err) => {
            let err = RepositoryError::from(err);
            ffx_bail!("Adding repository {:} failed: {}", cmd.name, err);
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
            AddFromPmCommand { name: "MyRepo".to_owned(), pm_repo_path: "a/b".into() },
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
                    path: Some("a/b".to_owned()),
                    ..PmRepositorySpec::EMPTY
                })
            )
        );
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_core::ffx_plugin,
    ffx_repository_add_from_pm_args::AddFromPmCommand,
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::{RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    std::convert::TryInto,
};

#[ffx_plugin(RepositoryRegistryProxy = "daemon::protocol")]
pub async fn add_from_pm(cmd: AddFromPmCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    // Validate that we can construct a valid repository url from the name.
    let repo_url = RepositoryUrl::parse_host(cmd.repository.to_string())
        .map_err(|err| ffx_error!("invalid repository name for {:?}: {}", cmd.repository, err))?;
    let repo_name = repo_url.host();

    let full_path = cmd
        .pm_repo_path
        .canonicalize()
        .with_context(|| format!("failed to canonicalize {:?}", cmd.pm_repo_path))?;

    let repo_spec = RepositorySpec::Pm { path: full_path.try_into()? };

    match repos.add_repository(repo_name, &mut repo_spec.into()).await? {
        Ok(()) => {
            println!("added repository {}", repo_name);
            Ok(())
        }
        Err(err) => {
            let err = RepositoryError::from(err);
            ffx_bail!("Adding repository {} failed: {}", repo_name, err);
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_developer_ffx::{PmRepositorySpec, RepositoryRegistryRequest, RepositorySpec},
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
                repository: "my-repo".to_owned(),
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
                "my-repo".to_owned(),
                RepositorySpec::Pm(PmRepositorySpec {
                    path: Some(tmp.path().canonicalize().unwrap().to_str().unwrap().to_string()),
                    ..PmRepositorySpec::EMPTY
                })
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_from_pm_rejects_invalid_names() {
        let tmp = tempfile::tempdir().unwrap();

        let repos =
            setup_fake_repos(move |req| panic!("should not receive any requests: {:?}", req));

        for name in ["", "my_repo", "MyRepo", "😀"] {
            assert_matches!(
                add_from_pm(
                    AddFromPmCommand {
                        repository: name.to_owned(),
                        pm_repo_path: tmp.path().to_path_buf(),
                    },
                    repos.clone(),
                )
                .await,
                Err(_)
            );
        }
    }
}

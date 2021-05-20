// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_add_args::AddCommand,
    fidl_fuchsia_developer_bridge::{FileSystemRepositorySpec, RepositoriesProxy, RepositorySpec},
};

#[ffx_plugin(RepositoriesProxy = "daemon::service")]
pub async fn add(cmd: AddCommand, repos: RepositoriesProxy) -> Result<()> {
    repos.add(
        &cmd.name,
        &mut RepositorySpec::Filesystem(FileSystemRepositorySpec {
            path: Some(cmd.repo_path.as_os_str().to_str().unwrap().to_owned()),
            ..FileSystemRepositorySpec::EMPTY
        }),
    )?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::{
            FileSystemRepositorySpec, RepositoriesRequest, RepositorySpec,
        },
        fuchsia_async as fasync,
        futures::channel::oneshot::channel,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_add() {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoriesRequest::Add { name, repository, .. } => {
                sender.take().unwrap().send((name, repository)).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        });
        add(AddCommand { name: "MyRepo".to_owned(), repo_path: "a/b".into() }, repos)
            .await
            .unwrap();
        let got = receiver.await.unwrap();
        assert_eq!(
            got,
            (
                "MyRepo".to_owned(),
                RepositorySpec::Filesystem(FileSystemRepositorySpec {
                    path: Some("a/b".to_owned()),
                    ..FileSystemRepositorySpec::EMPTY
                })
            )
        );
    }
}

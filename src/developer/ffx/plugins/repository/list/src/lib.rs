// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_list_args::ListCommand,
    fidl,
    fidl_fuchsia_developer_bridge::{RepositoryIteratorMarker, RepositoryRegistryProxy},
    std::io::{stdout, Write},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn list(cmd: ListCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    list_impl(cmd, repos, stdout()).await
}

async fn list_impl<W: Write>(
    _cmd: ListCommand,
    repos_proxy: RepositoryRegistryProxy,
    mut writer: W,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints::<RepositoryIteratorMarker>()?;
    repos_proxy.list_repositories(server)?;
    let client = client.into_proxy()?;

    loop {
        let repos = client.next().await?;

        if repos.is_empty() {
            return Ok(());
        }

        for repo in repos {
            writeln!(writer, "{}", repo.name)?;
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::{
            FileSystemRepositorySpec, RepositoryConfig, RepositoryIteratorRequest,
            RepositoryRegistryRequest, RepositorySpec,
        },
        fuchsia_async as fasync,
        futures::StreamExt,
    };

    #[fasync::run_singlethreaded(test)]
    async fn list() {
        let repos = setup_fake_repos(move |req| {
            fasync::Task::spawn(async move {
                let mut sent = false;
                match req {
                    RepositoryRegistryRequest::ListRepositories { iterator, .. } => {
                        let mut iterator = iterator.into_stream().unwrap();
                        while let Some(Ok(req)) = iterator.next().await {
                            match req {
                                RepositoryIteratorRequest::Next { responder } => {
                                    if !sent {
                                        sent = true;
                                        responder
                                            .send(
                                                &mut vec![
                                                    &mut RepositoryConfig {
                                                        name: "Test1".to_owned(),
                                                        spec: RepositorySpec::FileSystem(
                                                            FileSystemRepositorySpec {
                                                                path: Some("a/b".to_owned()),
                                                                ..FileSystemRepositorySpec::EMPTY
                                                            },
                                                        ),
                                                    },
                                                    &mut RepositoryConfig {
                                                        name: "Test2".to_owned(),
                                                        spec: RepositorySpec::FileSystem(
                                                            FileSystemRepositorySpec {
                                                                path: Some("c/d".to_owned()),
                                                                ..FileSystemRepositorySpec::EMPTY
                                                            },
                                                        ),
                                                    },
                                                ]
                                                .into_iter(),
                                            )
                                            .unwrap()
                                    } else {
                                        responder.send(&mut vec![].into_iter()).unwrap()
                                    }
                                }
                            }
                        }
                    }
                    other => panic!("Unexpected request: {:?}", other),
                }
            })
            .detach();
        });
        let mut out = Vec::<u8>::new();
        list_impl(ListCommand {}, repos, &mut out).await.unwrap();
        assert_eq!(&String::from_utf8_lossy(&out), "Test1\nTest2\n");
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_core::ffx_plugin,
    ffx_repository_target_list_args::ListCommand,
    fidl_fuchsia_developer_bridge::{RepositoryRegistryProxy, RepositoryStorageType},
    prettytable::{cell, row, Table},
    std::{
        collections::HashMap,
        io::{stdout, Write},
    },
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn list_cmd(cmd: ListCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    list_impl(cmd, repos, stdout()).await
}

async fn list_impl<W: Write>(
    _cmd: ListCommand,
    repos: RepositoryRegistryProxy,
    mut writer: W,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints()?;
    repos.list_registered_targets(server).context("communicating with daemon")?;
    let registered_targets = client.into_proxy()?;

    let mut items = HashMap::new();

    while let Some(registered_targets) =
        registered_targets.next().await.map(|x| if x.is_empty() { None } else { Some(x) })?
    {
        for registered_target in registered_targets {
            let repo = registered_target.repo_name.unwrap_or("<unknown>".to_owned());
            let mut target_identifier =
                registered_target.target_identifier.unwrap_or("<unknown>".to_owned());

            if registered_target.storage_type == Some(RepositoryStorageType::Ephemeral) {
                target_identifier.push_str(" (EPHEMERAL)")
            }

            let mut aliases = registered_target.aliases.unwrap_or_else(Vec::new);
            aliases.sort();

            for alias in aliases {
                target_identifier.push_str(&format!("\n  alias: {}", alias));
            }

            items.entry(repo).or_insert_with(Vec::new).push(target_identifier);
        }
    }
    if items.is_empty() {
        return Ok(());
    }

    for value in items.values_mut() {
        value.sort();
    }

    let mut items = items.into_iter().collect::<Vec<_>>();
    items.sort_by(|(x, _), (y, _)| x.cmp(&y));

    let mut table = Table::new();
    table.set_titles(row!("REPO", "TARGET"));

    for (repo, targets) in items {
        table.add_row(row!(repo, targets.join("\n"),));
    }

    table.print(&mut writer)?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::{
            RepositoryRegistryRequest, RepositoryTarget, RepositoryTargetsIteratorRequest,
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
                    RepositoryRegistryRequest::ListRegisteredTargets { iterator, .. } => {
                        let mut iterator = iterator.into_stream().unwrap();
                        while let Some(Ok(req)) = iterator.next().await {
                            match req {
                                RepositoryTargetsIteratorRequest::Next { responder } => {
                                    if !sent {
                                        sent = true;
                                        responder
                                            .send(
                                                &mut vec![
                                                    RepositoryTarget {
                                                        repo_name: Some("bob".to_owned()),
                                                        target_identifier: Some(
                                                            "target1".to_owned(),
                                                        ),
                                                        aliases: Some(vec![
                                                            "target1_alias1".to_owned(),
                                                            "target1_alias2".to_owned(),
                                                        ]),
                                                        ..RepositoryTarget::EMPTY
                                                    },
                                                    RepositoryTarget {
                                                        repo_name: Some("smith".to_owned()),
                                                        target_identifier: Some(
                                                            "target2".to_owned(),
                                                        ),
                                                        storage_type: Some(
                                                            RepositoryStorageType::Ephemeral,
                                                        ),
                                                        ..RepositoryTarget::EMPTY
                                                    },
                                                    RepositoryTarget {
                                                        repo_name: Some("bob".to_owned()),
                                                        target_identifier: Some(
                                                            "target3".to_owned(),
                                                        ),
                                                        ..RepositoryTarget::EMPTY
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

        static EXPECT: &str = "\
             +-------+-------------------------+\n\
             | REPO  | TARGET                  |\n\
             +=======+=========================+\n\
             | bob   | target1                 |\n\
             |       |   alias: target1_alias1 |\n\
             |       |   alias: target1_alias2 |\n\
             |       | target3                 |\n\
             +-------+-------------------------+\n\
             | smith | target2 (EPHEMERAL)     |\n\
             +-------+-------------------------+\n";

        assert_eq!(EXPECT, &String::from_utf8_lossy(&out));
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    chrono::{offset::Utc, DateTime},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_packages_args::PackagesCommand,
    fidl,
    fidl_fuchsia_developer_bridge::{RepositoryPackagesIteratorMarker, RepositoryRegistryProxy},
    fidl_fuchsia_developer_bridge_ext::RepositoryError,
    humansize::{file_size_opts, FileSize},
    prettytable::{cell, format::TableFormat, row, Row, Table},
    std::{
        io::{stdout, Write},
        time::{Duration, SystemTime},
    },
};

const MAX_HASH: usize = 11;

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn packages(cmd: PackagesCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    packages_impl(cmd, repos, None, stdout()).await
}

async fn packages_impl<W: Write>(
    cmd: PackagesCommand,
    repos_proxy: RepositoryRegistryProxy,
    table_format: Option<TableFormat>,
    mut writer: W,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints::<RepositoryPackagesIteratorMarker>()?;

    let repo_name = if let Some(repo_name) = cmd.repository().await? {
        repo_name
    } else {
        ffx_bail!(
            "Either a default repository must be set, or the --repository flag must be provided.\n\
            You can set a default repository using: `ffx repository default set <name>`."
        )
    };

    match repos_proxy.list_packages(&repo_name, server).await? {
        Ok(()) => {}
        Err(err) => match RepositoryError::from(err) {
            RepositoryError::NoMatchingRepository => {
                ffx_bail!("repository {:?} does not exist", repo_name)
            }
            err => ffx_bail!("error listing packages: {}", err),
        },
    }

    let client = client.into_proxy()?;

    let mut table = Table::new();
    table.set_titles(row!("NAME", "SIZE", "HASH", "MODIFIED"));
    if let Some(fmt) = table_format {
        table.set_format(fmt);
    }

    let mut rows = vec![];
    loop {
        let repos = client.next().await?;

        if repos.is_empty() {
            rows.sort_by_key(|r: &Row| r.get_cell(0).unwrap().get_content());
            for row in rows.into_iter() {
                table.add_row(row);
            }
            table.print(&mut writer)?;
            return Ok(());
        }

        for repo in repos {
            rows.push(row!(
                repo.name.as_deref().unwrap_or("<unknown>"),
                repo.size
                    .map(|s| s
                        .file_size(file_size_opts::CONVENTIONAL)
                        .unwrap_or_else(|_| format!("{}b", s)))
                    .unwrap_or_else(|| "<unknown>".to_string()),
                repo.hash
                    .map(|s| {
                        if cmd.full_hash {
                            s
                        } else {
                            let mut clone = s.clone();
                            clone.truncate(MAX_HASH);
                            clone.push_str("...");
                            clone
                        }
                    })
                    .unwrap_or_else(|| "<unknown>".to_string()),
                repo.modified
                    .and_then(|m| SystemTime::UNIX_EPOCH.checked_add(Duration::from_secs(m)))
                    .map(|m| DateTime::<Utc>::from(m).to_rfc2822())
                    .unwrap_or_else(String::new)
            ));
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::{
            RepositoryPackage, RepositoryPackagesIteratorRequest, RepositoryRegistryRequest,
        },
        fuchsia_async as fasync,
        futures::StreamExt,
        prettytable::format::FormatBuilder,
    };

    async fn setup_repo_proxy() -> RepositoryRegistryProxy {
        setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ListPackages { name, iterator, responder } => {
                assert_eq!(name, "devhost");

                fasync::Task::spawn(async move {
                    let mut sent = false;
                    let mut iterator = iterator.into_stream().unwrap();
                    while let Some(Ok(req)) = iterator.next().await {
                        match req {
                            RepositoryPackagesIteratorRequest::Next { responder } => {
                                if !sent {
                                    sent = true;
                                    responder
                                        .send(
                                            &mut vec![
                                                RepositoryPackage {
                                                    name: Some("package1".to_string()),
                                                    size: Some(1),
                                                    hash: Some(
                                                        "longhashlonghashlonghashlonghash"
                                                            .to_string(),
                                                    ),
                                                    modified: Some(60 * 60 * 24),
                                                    ..RepositoryPackage::EMPTY
                                                },
                                                RepositoryPackage {
                                                    name: Some("package2".to_string()),
                                                    size: Some(2048),
                                                    hash: Some(
                                                        "secondhashsecondhashsecondhash"
                                                            .to_string(),
                                                    ),
                                                    ..RepositoryPackage::EMPTY
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
                })
                .detach();
                responder.send(&mut Ok(())).unwrap();
            }
            other => panic!("Unexpected request: {:?}", other),
        })
    }

    async fn run_impl(cmd: PackagesCommand) -> String {
        let repos = setup_repo_proxy().await;
        let mut out = Vec::<u8>::new();
        timeout::timeout(
            std::time::Duration::from_millis(1000),
            packages_impl(cmd, repos, Some(FormatBuilder::new().padding(1, 1).build()), &mut out),
        )
        .await
        .unwrap()
        .unwrap();

        String::from_utf8_lossy(&out).to_string()
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_list_truncated_hash() {
        assert_eq!(run_impl(PackagesCommand {
            repository: Some("devhost".to_string()),
            full_hash: false,
        }).await.trim(), "NAME      SIZE  HASH            MODIFIED \n package1  1 B   longhashlon...  Fri, 02 Jan 1970 00:00:00 +0000 \n package2  2 KB  secondhashs...");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_list_full_hash() {
        assert_eq!(run_impl(PackagesCommand {
            repository: Some("devhost".to_string()),
            full_hash: true,
        }).await.trim(), "NAME      SIZE  HASH                              MODIFIED \n package1  1 B   longhashlonghashlonghashlonghash  Fri, 02 Jan 1970 00:00:00 +0000 \n package2  2 KB  secondhashsecondhashsecondhash");
    }
}

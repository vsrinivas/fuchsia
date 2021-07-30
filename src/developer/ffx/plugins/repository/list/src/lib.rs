// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_list_args::ListCommand,
    fidl,
    fidl_fuchsia_developer_bridge::{
        self as bridge, RepositoryIteratorMarker, RepositoryRegistryProxy,
    },
    prettytable::{cell, format::TableFormat, row, Row, Table},
    std::io::{stdout, Write},
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::service")]
pub async fn list(cmd: ListCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    list_impl(cmd, repos, None, stdout()).await
}

async fn list_impl<W: Write>(
    _cmd: ListCommand,
    repos_proxy: RepositoryRegistryProxy,
    table_format: Option<TableFormat>,
    mut writer: W,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints::<RepositoryIteratorMarker>()?;
    repos_proxy.list_repositories(server)?;
    let client = client.into_proxy()?;

    let mut table = Table::new();
    table.set_titles(row!("NAME", "TYPE", "EXTRA"));
    if let Some(fmt) = table_format {
        table.set_format(fmt);
    }

    let default_repo = pkg::config::get_default_repository().await?;

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
            let mut row = row!();

            if default_repo.as_deref() == Some(&repo.name) {
                row.add_cell(cell!(format!("{}*", repo.name)));
            } else {
                row.add_cell(cell!(repo.name));
            }

            match repo.spec {
                bridge::RepositorySpec::FileSystem(filesystem_spec) => {
                    row.add_cell(cell!("filesystem"));
                    row.add_cell(cell!(filesystem_spec.path.as_deref().unwrap_or("<unknown>")));
                }
                bridge::RepositorySpec::Pm(pm_spec) => {
                    row.add_cell(cell!("pm"));
                    row.add_cell(cell!(pm_spec.path.as_deref().unwrap_or("<unknown>")));
                }
                bridge::RepositorySpecUnknown!() => {
                    row.add_cell(cell!("<unknown>"));
                    row.add_cell(cell!(""));
                }
            }

            rows.push(row);
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::{
            FileSystemRepositorySpec, PmRepositorySpec, RepositoryConfig,
            RepositoryIteratorRequest, RepositoryRegistryRequest, RepositorySpec,
        },
        fuchsia_async as fasync,
        futures::StreamExt,
        prettytable::format::FormatBuilder,
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
                                                        spec: RepositorySpec::Pm(
                                                            PmRepositorySpec {
                                                                path: Some("c/d".to_owned()),
                                                                ..PmRepositorySpec::EMPTY
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
        list_impl(
            ListCommand {},
            repos,
            Some(FormatBuilder::new().padding(1, 1).build()),
            &mut out,
        )
        .await
        .unwrap();
        assert_eq!(
            &String::from_utf8_lossy(&out),
            " NAME   TYPE        EXTRA \n Test1  filesystem  a/b \n Test2  pm          c/d \n"
        );
    }
}

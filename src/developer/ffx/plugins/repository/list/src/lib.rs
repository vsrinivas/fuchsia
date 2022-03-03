// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    ffx_core::ffx_plugin,
    ffx_repository_list_args::ListCommand,
    ffx_writer::Writer,
    fidl,
    fidl_fuchsia_developer_ffx::{RepositoryIteratorMarker, RepositoryRegistryProxy},
    fidl_fuchsia_developer_ffx_ext::{RepositoryConfig, RepositorySpec},
    prettytable::{cell, format::TableFormat, row, table, Table},
    std::convert::TryInto,
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn list(
    cmd: ListCommand,
    repos: RepositoryRegistryProxy,
    #[ffx(machine = Vec<RepositoryConfig>)] mut writer: Writer,
) -> Result<()> {
    list_impl(cmd, repos, None, &mut writer).await
}

async fn list_impl(
    _cmd: ListCommand,
    repos_proxy: RepositoryRegistryProxy,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints::<RepositoryIteratorMarker>()
        .context("creating endpoints")?;
    repos_proxy.list_repositories(server).context("listing repositories")?;
    let client = client.into_proxy().context("creating repository iterator proxy")?;

    let default_repo =
        pkg::config::get_default_repository().await.context("getting default repository")?;

    let mut repos = vec![];
    loop {
        let batch = client.next().await.context("fetching next batch of repositories")?;
        if batch.is_empty() {
            break;
        }

        for repo in batch {
            repos.push(repo.try_into().context("converting repository config")?);
        }
    }

    repos.sort();

    if writer.is_machine() {
        writer.machine(&repos).context("writing machine representation of repositories")?;
    } else {
        print_table(&repos, default_repo, table_format, writer)
            .context("printing repository table")?
    }

    Ok(())
}

fn print_table(
    repos: &[RepositoryConfig],
    default_repo: Option<String>,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let mut table = Table::new();
    table.set_titles(row!("NAME", "TYPE", "EXTRA"));
    if let Some(fmt) = table_format {
        table.set_format(fmt);
    }

    let mut rows = vec![];

    for repo in repos {
        let mut row = row!();

        if default_repo.as_deref() == Some(&repo.name) {
            row.add_cell(cell!(format!("{}*", repo.name)));
        } else {
            row.add_cell(cell!(repo.name));
        }

        match &repo.spec {
            RepositorySpec::FileSystem { metadata_repo_path, blob_repo_path } => {
                row.add_cell(cell!("filesystem"));
                row.add_cell(cell!(table!(
                    ["metadata", metadata_repo_path],
                    ["blobs", blob_repo_path]
                )));
            }
            RepositorySpec::Pm { path } => {
                row.add_cell(cell!("pm"));
                row.add_cell(cell!(path));
            }
            RepositorySpec::Http { metadata_repo_url, blob_repo_url } => {
                row.add_cell(cell!("http"));
                row.add_cell(cell!(table!(
                    ["metadata", metadata_repo_url],
                    ["blobs", blob_repo_url]
                )));
            }
        }

        rows.push(row);
    }

    for row in rows.into_iter() {
        table.add_row(row);
    }

    table.print(writer).context("printing table to writer")?;

    return Ok(());
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_ffx::{
            FileSystemRepositorySpec, PmRepositorySpec, RepositoryConfig,
            RepositoryIteratorRequest, RepositoryRegistryProxy, RepositoryRegistryRequest,
            RepositorySpec,
        },
        fuchsia_async as fasync,
        futures::StreamExt,
    };

    fn fake_repos() -> RepositoryRegistryProxy {
        setup_fake_repos(move |req| {
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
                                                                metadata_repo_path: Some(
                                                                    "a/b/meta".to_owned(),
                                                                ),
                                                                blob_repo_path: Some(
                                                                    "a/b/blobs".to_owned(),
                                                                ),
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
        })
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list() {
        let repos = fake_repos();
        let mut out = Writer::new_test(None);
        list_impl(ListCommand {}, repos, None, &mut out).await.unwrap();

        assert_eq!(
            &out.test_output().unwrap(),
            "\
            +-------+------------+--------------------------+\n\
            | NAME  | TYPE       | EXTRA                    |\n\
            +=======+============+==========================+\n\
            | Test1 | filesystem | +----------+-----------+ |\n\
            |       |            | | metadata | a/b/meta  | |\n\
            |       |            | +----------+-----------+ |\n\
            |       |            | | blobs    | a/b/blobs | |\n\
            |       |            | +----------+-----------+ |\n\
            +-------+------------+--------------------------+\n\
            | Test2 | pm         | c/d                      |\n\
            +-------+------------+--------------------------+\n",
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_machine() {
        let repos = fake_repos();
        let mut out = Writer::new_test(Some(ffx_writer::Format::Json));
        list_impl(ListCommand {}, repos, None, &mut out).await.unwrap();

        assert_eq!(
            serde_json::from_str::<serde_json::Value>(&out.test_output().unwrap()).unwrap(),
            serde_json::json!([
                {
                    "name": "Test1",
                    "spec": {
                        "type": "file_system",
                        "metadata_repo_path": "a/b/meta",
                        "blob_repo_path": "a/b/blobs",
                    },
                },
                {
                    "name": "Test2",
                    "spec": {
                        "type": "pm",
                        "path": "c/d",
                    },
                },
            ]),
        );
    }
}

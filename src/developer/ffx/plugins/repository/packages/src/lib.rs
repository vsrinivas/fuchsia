// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    chrono::{offset::Utc, DateTime},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_packages_args::{
        ListSubCommand, PackagesCommand, PackagesSubCommand, ShowSubCommand,
    },
    ffx_writer::Writer,
    fidl_fuchsia_developer_ffx::{
        ListFields, PackageEntryIteratorMarker, RepositoryPackagesIteratorMarker,
        RepositoryRegistryProxy,
    },
    fidl_fuchsia_developer_ffx_ext::{PackageEntry, RepositoryError, RepositoryPackage},
    humansize::{file_size_opts, FileSize},
    prettytable::{cell, format::TableFormat, row, Row, Table},
    std::time::{Duration, SystemTime},
};

const MAX_HASH: usize = 11;

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn packages(
    cmd: PackagesCommand,
    repos: RepositoryRegistryProxy,
    #[ffx(machine = Vec<T:Serialize>)] mut writer: Writer,
) -> Result<()> {
    match cmd.subcommand {
        PackagesSubCommand::List(subcmd) => list_impl(subcmd, repos, None, &mut writer).await,
        PackagesSubCommand::Show(subcmd) => show_impl(subcmd, repos, None, &mut writer).await,
    }
}

async fn show_impl(
    cmd: ShowSubCommand,
    repos_proxy: RepositoryRegistryProxy,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints::<PackageEntryIteratorMarker>()?;

    let repo_name = if let Some(repo_name) = cmd.repository.clone() {
        repo_name
    } else if let Some(repo_name) = pkg::config::get_default_repository().await? {
        repo_name
    } else {
        ffx_bail!(
            "Either a default repository must be set, or the -r flag must be provided.\n\
                You can set a default repository using: `ffx repository default set <name>`."
        )
    };

    match repos_proxy.show_package(&repo_name, &cmd.package, server).await? {
        Ok(()) => {}
        Err(err) => match RepositoryError::from(err) {
            RepositoryError::NoMatchingRepository => {
                ffx_bail!("repository {:?} does not exist", repo_name)
            }
            err => ffx_bail!("error listing contents: {}", err),
        },
    }

    let client = client.into_proxy()?;

    let mut blobs: Vec<PackageEntry> = vec![];
    loop {
        let batch = client.next().await.context("fetching next batch of blobs")?;
        if batch.is_empty() {
            break;
        }

        for blob in batch {
            blobs.push((&blob).into());
        }
    }

    blobs.sort();

    if writer.is_machine() {
        writer.machine(&blobs).context("writing machine representation of blobs")?;
    } else {
        print_blob_table(&cmd, &blobs, table_format, writer).context("printing repository table")?
    }

    Ok(())
}

fn print_blob_table(
    cmd: &ShowSubCommand,
    blobs: &[PackageEntry],
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let mut table = Table::new();
    let header = row!("NAME", "SIZE", "HASH", "MODIFIED");
    table.set_titles(header);
    if let Some(fmt) = table_format {
        table.set_format(fmt);
    }

    let mut rows = vec![];

    for blob in blobs {
        let row = row!(
            blob.path.as_deref().unwrap_or("<unknown>"),
            blob.size
                .map(|s| s
                    .file_size(file_size_opts::CONVENTIONAL)
                    .unwrap_or_else(|_| format!("{}b", s)))
                .unwrap_or_else(|| "<unknown>".to_string()),
            format_hash(&blob.hash, cmd.full_hash),
            blob.modified
                .and_then(|m| SystemTime::UNIX_EPOCH.checked_add(Duration::from_secs(m)))
                .map(|m| DateTime::<Utc>::from(m).to_rfc2822())
                .unwrap_or_else(String::new)
        );
        rows.push(row);
    }

    for row in rows.into_iter() {
        table.add_row(row);
    }
    table.print(writer)?;

    Ok(())
}

async fn list_impl(
    cmd: ListSubCommand,
    repos_proxy: RepositoryRegistryProxy,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let (client, server) = fidl::endpoints::create_endpoints::<RepositoryPackagesIteratorMarker>()?;

    let repo_name = if let Some(repo_name) = cmd.repository.clone() {
        repo_name
    } else if let Some(repo_name) = pkg::config::get_default_repository().await? {
        repo_name
    } else {
        ffx_bail!(
            "Either a default repository must be set, or the --repository flag must be provided.\n\
                You can set a default repository using: `ffx repository default set <name>`."
        )
    };

    match repos_proxy
        .list_packages(
            &repo_name,
            server,
            if cmd.include_components { ListFields::COMPONENTS } else { ListFields::empty() },
        )
        .await?
    {
        Ok(()) => {}
        Err(err) => match RepositoryError::from(err) {
            RepositoryError::NoMatchingRepository => {
                ffx_bail!("repository {:?} does not exist", repo_name)
            }
            err => ffx_bail!("error listing packages: {}", err),
        },
    }

    let client = client.into_proxy()?;

    let mut packages: Vec<RepositoryPackage> = vec![];
    loop {
        let batch = client.next().await.context("fetching next batch of packages")?;
        if batch.is_empty() {
            break;
        }

        for package in batch {
            packages.push(package.try_into().context("converting repository package")?);
        }
    }

    packages.sort();

    if writer.is_machine() {
        writer.machine(&packages).context("writing machine representation of packages")?;
    } else {
        print_package_table(&cmd, &packages, table_format, writer)
            .context("printing repository table")?
    }

    Ok(())
}

fn print_package_table(
    cmd: &ListSubCommand,
    packages: &[RepositoryPackage],
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let mut table = Table::new();
    let mut header = row!("NAME", "SIZE", "HASH", "MODIFIED");
    if cmd.include_components {
        header.add_cell(cell!("COMPONENTS"));
    }
    table.set_titles(header);
    if let Some(fmt) = table_format {
        table.set_format(fmt);
    }

    let mut rows = vec![];

    for pkg in packages {
        let mut row = row!(
            pkg.name.as_deref().unwrap_or("<unknown>"),
            pkg.size
                .map(|s| s
                    .file_size(file_size_opts::CONVENTIONAL)
                    .unwrap_or_else(|_| format!("{}b", s)))
                .unwrap_or_else(|| "<unknown>".to_string()),
            format_hash(&pkg.hash, cmd.full_hash),
            pkg.modified
                .and_then(|m| SystemTime::UNIX_EPOCH.checked_add(Duration::from_secs(m)))
                .map(|m| DateTime::<Utc>::from(m).to_rfc2822())
                .unwrap_or_else(String::new)
        );
        if cmd.include_components {
            row.add_cell(cell!(pkg
                .entries
                .iter()
                .map(|p| p
                    .path
                    .as_ref()
                    .cloned()
                    .unwrap_or_else(|| String::from("<missing manifest path>")))
                .collect::<Vec<_>>()
                .join("\n")));
        }
        rows.push(row);
    }

    rows.sort_by_key(|r: &Row| r.get_cell(0).unwrap().get_content());
    for row in rows.into_iter() {
        table.add_row(row);
    }
    table.print(writer)?;

    Ok(())
}

fn format_hash(hash_value: &Option<String>, full: bool) -> String {
    if let Some(value) = hash_value {
        if full {
            value.to_string()
        } else {
            let clone = value.to_string();
            format!("{}...", &clone[..MAX_HASH])
        }
    } else {
        "unknown".to_string()
    }
}

#[cfg(test)]
mod test {
    use fidl_fuchsia_developer_ffx::PackageEntryIteratorRequest;

    use super::*;
    use {
        fidl_fuchsia_developer_ffx::{
            PackageEntry, RepositoryPackage, RepositoryPackagesIteratorRequest,
            RepositoryRegistryRequest,
        },
        fuchsia_async as fasync,
        futures::StreamExt,
        prettytable::format::FormatBuilder,
    };

    fn component(names: Vec<&str>) -> Vec<PackageEntry> {
        names
            .iter()
            .map(|n| PackageEntry { path: Some(n.to_string()), ..PackageEntry::EMPTY })
            .collect()
    }
    async fn setup_repo_proxy(expected_include_fields: ListFields) -> RepositoryRegistryProxy {
        setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ListPackages {
                name,
                iterator,
                responder,
                include_fields,
            } => {
                assert_eq!(name, "devhost");
                assert_eq!(include_fields, expected_include_fields);

                fasync::Task::spawn(async move {
                    let mut sent = false;
                    let mut iterator = iterator.into_stream().unwrap();
                    while let Some(Ok(req)) = iterator.next().await {
                        match req {
                            RepositoryPackagesIteratorRequest::Next { responder } => {
                                if !sent {
                                    sent = true;
                                    let pkg1_components =
                                        if include_fields.intersects(ListFields::COMPONENTS) {
                                            Some(component(vec!["component1"]))
                                        } else {
                                            None
                                        };
                                    let pkg2_components =
                                        if include_fields.intersects(ListFields::COMPONENTS) {
                                            Some(component(vec!["component2", "component3"]))
                                        } else {
                                            None
                                        };
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
                                                    entries: pkg1_components,
                                                    ..RepositoryPackage::EMPTY
                                                },
                                                RepositoryPackage {
                                                    name: Some("package2".to_string()),
                                                    size: Some(2048),
                                                    hash: Some(
                                                        "secondhashsecondhashsecondhash"
                                                            .to_string(),
                                                    ),
                                                    entries: pkg2_components,
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
            RepositoryRegistryRequest::ShowPackage {
                repository_name,
                package_name,
                iterator,
                responder,
            } => {
                assert_eq!(repository_name, "devhost");
                assert_eq!(package_name, "package");

                fasync::Task::spawn(async move {
                    let mut sent = false;
                    let mut iterator = iterator.into_stream().unwrap();
                    while let Some(Ok(req)) = iterator.next().await {
                        match req {
                            PackageEntryIteratorRequest::Next { responder } => {
                                if !sent {
                                    sent = true;
                                    responder
                                        .send(
                                            &mut vec![
                                                PackageEntry {
                                                    path: Some("meta.far".to_string()),
                                                    size: Some(1),
                                                    hash: Some(
                                                        "longhashlonghashlonghashlonghash"
                                                            .to_string(),
                                                    ),
                                                    modified: Some(60 * 60 * 24),
                                                    ..PackageEntry::EMPTY
                                                },
                                                PackageEntry {
                                                    path: Some("blob2".to_string()),
                                                    size: Some(2048),
                                                    hash: Some(
                                                        "secondhashsecondhashsecondhash"
                                                            .to_string(),
                                                    ),
                                                    ..PackageEntry::EMPTY
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

    async fn run_impl(cmd: ListSubCommand, writer: &mut Writer) {
        let repos = setup_repo_proxy(if cmd.include_components {
            ListFields::COMPONENTS
        } else {
            ListFields::empty()
        })
        .await;
        timeout::timeout(
            std::time::Duration::from_millis(1000),
            list_impl(cmd, repos, Some(FormatBuilder::new().padding(1, 1).build()), writer),
        )
        .await
        .unwrap()
        .unwrap();
    }

    async fn run_impl_for_show_command(cmd: ShowSubCommand, writer: &mut Writer) {
        let repos = setup_repo_proxy(ListFields::empty()).await;

        timeout::timeout(
            std::time::Duration::from_millis(1000),
            show_impl(cmd, repos, Some(FormatBuilder::new().padding(1, 1).build()), writer),
        )
        .await
        .unwrap()
        .unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_list_truncated_hash() -> Result<()> {
        let mut writer = Writer::new_test(None);

        run_impl(
            ListSubCommand {
                repository: Some("devhost".to_string()),
                full_hash: false,
                include_components: false,
            },
            &mut writer,
        )
        .await;
        let actual = writer.test_output()?;
        assert_eq!(
            actual,
            " NAME      SIZE  HASH            MODIFIED \n \
                          package1  1 B   longhashlon...  Fri, 02 Jan 1970 00:00:00 +0000 \n \
                          package2  2 KB  secondhashs...   \n"
        );
        assert!(writer.test_error()?.is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_list_full_hash() -> Result<()> {
        let mut writer = Writer::new_test(None);

        run_impl(
            ListSubCommand {
                repository: Some("devhost".to_string()),
                full_hash: true,
                include_components: false,
            },
            &mut writer,
        )
        .await;
        let actual = writer.test_output()?;
        assert_eq!(
            actual,
            " NAME      SIZE  HASH                              MODIFIED \n \
            package1  1 B   longhashlonghashlonghashlonghash  Fri, 02 Jan 1970 00:00:00 +0000 \n \
            package2  2 KB  secondhashsecondhashsecondhash     \n"
        );
        assert!(writer.test_error()?.is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_list_including_components() -> Result<()> {
        let mut writer = Writer::new_test(None);

        run_impl(
            ListSubCommand {
                repository: Some("devhost".to_string()),
                full_hash: false,
                include_components: true,
            },
            &mut writer,
        )
        .await;

        let actual = writer.test_output()?;

        assert_eq!(actual, " NAME      SIZE  HASH            MODIFIED                         COMPONENTS \n \
                          package1  1 B   longhashlon...  Fri, 02 Jan 1970 00:00:00 +0000  component1 \n \
                          package2  2 KB  secondhashs...                                   component2 \n                                                                  \
                                                                                           component3 \n");
        assert!(writer.test_error()?.is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_show_package_truncated_hash() -> Result<()> {
        let mut writer = Writer::new_test(None);

        run_impl_for_show_command(
            ShowSubCommand {
                repository: Some("devhost".to_string()),
                full_hash: false,
                package: "package".to_string(),
            },
            &mut writer,
        )
        .await;
        let actual = writer.test_output()?;
        assert_eq!(
            actual,
            " NAME      SIZE  HASH            MODIFIED \n \
            blob2     2 KB  secondhashs...   \n \
            meta.far  1 B   longhashlon...  Fri, 02 Jan 1970 00:00:00 +0000 \n"
        );
        assert!(writer.test_error()?.is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_show_package_full_hash() -> Result<()> {
        let mut writer = Writer::new_test(None);

        run_impl_for_show_command(
            ShowSubCommand {
                repository: Some("devhost".to_string()),
                full_hash: true,
                package: "package".to_string(),
            },
            &mut writer,
        )
        .await;
        let actual = writer.test_output()?;
        assert_eq!(
            actual,
            " NAME      SIZE  HASH                              MODIFIED \n \
            blob2     2 KB  secondhashsecondhashsecondhash     \n \
            meta.far  1 B   longhashlonghashlonghashlonghash  Fri, 02 Jan 1970 00:00:00 +0000 \n"
        );
        assert!(writer.test_error()?.is_empty());
        Ok(())
    }
}

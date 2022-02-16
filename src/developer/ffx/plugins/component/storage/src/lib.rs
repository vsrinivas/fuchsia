// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::ffx_bail,
    ffx_component_storage_args::{Provider, StorageCommand, SubcommandEnum},
    ffx_core::ffx_plugin,
    fidl::handle::Channel,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_sys2::StorageAdminProxy,
    selectors::{self, VerboseError},
    std::io::{stdout, Write},
    std::path::{Component, PathBuf},
};

mod copy;
mod list;
mod make_directory;

#[ffx_plugin()]
pub async fn storage(remote_proxy: RemoteControlProxy, args: StorageCommand) -> Result<()> {
    let mut write = Box::new(stdout());
    let writer = &mut write;
    let selector = selectors::parse_selector::<VerboseError>(match args.provider {
        Provider::Data => &"core:expose:fuchsia.sys2.StorageAdmin"[..],
        Provider::Cache => &"core:expose:fuchsia.sys2.StorageAdmin.cache"[..],
        Provider::Temp => &"core:expose:fuchsia.sys2.StorageAdmin.tmp"[..],
    })
    .unwrap();

    let (client, server) = Channel::create()?;

    match remote_proxy.connect(selector, server).await.context("awaiting connect call")? {
        Ok(_) => {
            let storage_admin =
                StorageAdminProxy::new(fidl::AsyncChannel::from_channel(client).unwrap());
            storage_cmd(storage_admin, args.subcommand).await
        }
        Err(e) => {
            writeln!(writer, "Failed to connect to service: {:?}", e)?;
            Ok(())
        }
    }
}

async fn storage_cmd(storage_admin: StorageAdminProxy, subcommand: SubcommandEnum) -> Result<()> {
    match subcommand {
        SubcommandEnum::Copy(args) => copy::copy(storage_admin, args).await,
        SubcommandEnum::List(args) => list::list(storage_admin, args).await,
        SubcommandEnum::MakeDirectory(args) => {
            make_directory::make_directory(storage_admin, args).await
        }
    }
}

pub const REMOTE_PATH_HELP: &'static str = "Remote paths have the following format:\n\n\
[component instance ID]::[path relative to storage]\n\n\
`..` is not valid anywhere in the remote path.";

pub struct RemotePath {
    pub component_instance_id: String,
    pub relative_path: PathBuf,
}

impl RemotePath {
    pub fn parse(input: &str) -> Result<Self> {
        match input.split_once("::") {
            Some((first, second)) => {
                if second.contains("::") {
                    ffx_bail!(
                        "Remote path must contain exactly one `::` separator. {}",
                        REMOTE_PATH_HELP
                    )
                }

                let component_instance_id = first.to_string();
                let relative_path = PathBuf::from(second);

                // Path checks (ignore `.`) (no `..`, `/` or prefix allowed).
                let mut normalized_relative_path = PathBuf::new();
                for component in relative_path.components() {
                    match component {
                        Component::Normal(c) => normalized_relative_path.push(c),
                        Component::RootDir => continue,
                        Component::CurDir => continue,
                        c => ffx_bail!("Unsupported path object: {:?}. {}", c, REMOTE_PATH_HELP),
                    }
                }

                Ok(Self { component_instance_id, relative_path: normalized_relative_path })
            }
            None => ffx_bail!(
                "Remote path must contain exactly one `::` separator. {}",
                REMOTE_PATH_HELP
            ),
        }
    }
}

#[cfg(test)]
pub mod test {
    use fidl_fuchsia_sys2::StorageAdminProxy;
    use futures::TryStreamExt;

    pub fn setup_oneshot_fake_storage_admin<R: 'static>(mut handle_request: R) -> StorageAdminProxy
    where
        R: FnMut(fidl::endpoints::Request<<StorageAdminProxy as fidl::endpoints::Proxy>::Protocol>),
    {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<
            <StorageAdminProxy as fidl::endpoints::Proxy>::Protocol,
        >()
        .unwrap();
        fuchsia_async::Task::local(async move {
            if let Ok(Some(req)) = stream.try_next().await {
                handle_request(req);
            }
        })
        .detach();
        proxy
    }
}

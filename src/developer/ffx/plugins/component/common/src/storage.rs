// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    std::path::{Component, PathBuf},
};

mod copy;
mod list;
mod make_directory;

pub use {copy::copy, list::list, make_directory::make_directory};

pub const REMOTE_PATH_HELP: &'static str = "Remote paths have the following format:\n\n\
[component instance ID]::[path relative to storage]\n\n\
Example: \"c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4::/path/to/file\"\n\n
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
    use {
        fidl::endpoints::{RequestStream, ServerEnd},
        fidl::handle::AsyncChannel,
        fidl_fuchsia_io::*,
        fidl_fuchsia_sys2::StorageAdminProxy,
        fidl_fuchsia_sys2::StorageAdminRequest,
        futures::TryStreamExt,
    };

    fn setup_oneshot_fake_storage_admin<R: 'static>(mut handle_request: R) -> StorageAdminProxy
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

    pub fn node_to_directory(object: ServerEnd<NodeMarker>) -> DirectoryRequestStream {
        DirectoryRequestStream::from_channel(
            AsyncChannel::from_channel(object.into_channel()).unwrap(),
        )
    }

    pub fn node_to_file(object: ServerEnd<NodeMarker>) -> FileRequestStream {
        FileRequestStream::from_channel(AsyncChannel::from_channel(object.into_channel()).unwrap())
    }

    pub fn setup_fake_storage_admin(
        expected_id: &'static str,
        setup_fake_directory_fn: fn(DirectoryRequestStream),
    ) -> StorageAdminProxy {
        setup_oneshot_fake_storage_admin(move |req| match req {
            StorageAdminRequest::OpenComponentStorageById { id, object, responder, .. } => {
                assert_eq!(expected_id, id);
                setup_fake_directory_fn(node_to_directory(object));
                responder.send(&mut Ok(())).unwrap();
            }
            _ => panic!("got unexpected {:?}", req),
        })
    }
}

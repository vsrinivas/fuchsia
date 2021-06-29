// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fdio::Namespace;
use fidl::endpoints::{Proxy, ServerEnd};
use fidl_fuchsia_io as fio;
use std::collections::{hash_map::Entry, HashMap};
use std::sync::Arc;
use vfs::directory::{entry::DirectoryEntry, helper::DirectlyMutable, mutable::simple as pfs};
use vfs::execution_scope::ExecutionScope;

type Directory = Arc<pfs::Simple>;

/// Helper for binding entries in the namespace of the current process.
///
/// Namespace entries for served protocols are unbound when `NamespaceBinder` is dropped.
pub struct NamespaceBinder {
    scope: ExecutionScope,
    dirs: HashMap<String, Directory>,
}

impl NamespaceBinder {
    pub fn new(scope: ExecutionScope) -> NamespaceBinder {
        NamespaceBinder { scope, dirs: HashMap::new() }
    }

    /// Serves a protocol at the given path in the namespace of the current process.
    ///
    /// `path` must be absolute, e.g. "/foo/bar", containing no "." nor ".." entries.
    /// It is relative to the root of the namespace.
    pub fn bind_at_path(
        &mut self,
        path: &str,
        entry: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Error> {
        let ns = Namespace::installed()?;

        let (dir_path, entry_name) =
            path.rsplit_once('/').ok_or_else(|| format_err!("path must be absolute"))?;

        let dir = match self.dirs.entry(dir_path.to_string()) {
            Entry::Occupied(map_entry) => map_entry.into_mut(),
            Entry::Vacant(map_entry) => {
                let dir = pfs::simple();

                let (proxy, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
                dir.clone().open(
                    self.scope.clone(),
                    fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                    fio::MODE_TYPE_DIRECTORY,
                    vfs::path::Path::empty(),
                    ServerEnd::new(server.into_channel()),
                );

                ns.bind(
                    dir_path,
                    proxy
                        .into_channel()
                        .map_err(|_| format_err!("Failed to convert DirectoryProxy into channel"))?
                        .into_zx_channel(),
                )?;
                map_entry.insert(dir)
            }
        };

        dir.add_entry(entry_name, entry)?;

        Ok(())
    }
}

impl Drop for NamespaceBinder {
    fn drop(&mut self) {
        let ns = Namespace::installed().unwrap();
        for path in self.dirs.keys() {
            ns.unbind(path).unwrap();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::ClientEnd;
    use fidl_test_placeholders as echo;
    use fuchsia_zircon as zx;
    use futures::TryStreamExt;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_bind_at_path() -> Result<(), Error> {
        const NAMESPACE_PATH: &str = "/some/path/in/the/namespace";

        let scope = ExecutionScope::new();
        let mut ns = NamespaceBinder::new(scope);

        let echo_entry =
            vfs::service::host(move |mut stream: echo::EchoRequestStream| async move {
                while let Ok(Some(request)) = stream.try_next().await {
                    let echo::EchoRequest::EchoString { value, responder } = request;
                    responder.send(Some(&value.unwrap())).expect("responder failed");
                }
            });
        ns.bind_at_path(NAMESPACE_PATH, echo_entry)?;

        let (client, server) = zx::Channel::create()?;
        fdio::service_connect(NAMESPACE_PATH, server)?;
        let proxy = ClientEnd::<echo::EchoMarker>::new(client).into_proxy()?;

        let res = proxy.echo_string(Some("hello world")).await?;
        assert_eq!(res, Some("hello world".to_string()));

        Ok(())
    }
}

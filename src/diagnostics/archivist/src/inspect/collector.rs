// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ImmutableString,
    fidl::endpoints::{DiscoverableProtocolMarker, Proxy},
    fidl_fuchsia_inspect::{TreeMarker, TreeProxy},
    fidl_fuchsia_inspect_deprecated::{InspectMarker, InspectProxy},
    fidl_fuchsia_io as fio, fuchsia_fs, fuchsia_zircon as zx,
    futures::stream::StreamExt,
    pin_utils::pin_mut,
    std::collections::HashMap,
    std::path::Path,
    tracing::error,
};

#[cfg(test)]
use futures::FutureExt;

/// Mapping from a diagnostics filename to the underlying encoding of that
/// diagnostics data.
pub type DataMap = HashMap<ImmutableString, InspectData>;

pub type Moniker = String;

/// Data associated with a component.
/// This data is stored by data collectors and passed by the collectors to processors.
#[derive(Debug)]
pub enum InspectData {
    /// A VMO containing data associated with the event.
    Vmo(zx::Vmo),

    /// A file containing data associated with the event.
    ///
    /// Because we can't synchronously retrieve file contents like we can for VMOs, this holds
    /// the full file contents. Future changes should make streaming ingestion feasible.
    File(Vec<u8>),

    /// A connection to a Tree service.
    Tree(TreeProxy),

    /// A connection to the deprecated Inspect service.
    DeprecatedFidl(InspectProxy),
}

fn maybe_load_service<P: DiscoverableProtocolMarker>(
    dir_proxy: &fio::DirectoryProxy,
    entry: &fuchsia_fs::directory::DirEntry,
) -> Result<Option<P::Proxy>, anyhow::Error> {
    if entry.name.ends_with(P::PROTOCOL_NAME) {
        let (proxy, server) = fidl::endpoints::create_proxy::<P>()?;
        fdio::service_connect_at(
            dir_proxy.as_channel().as_ref(),
            &entry.name,
            server.into_channel(),
        )?;
        return Ok(Some(proxy));
    }
    Ok(None)
}

/// Searches the directory specified by inspect_directory_proxy for
/// .inspect files and populates the `inspect_data_map` with the found VMOs.
pub async fn populate_data_map(inspect_proxy: &fio::DirectoryProxy) -> DataMap {
    // TODO(fxbug.dev/36762): Use a streaming and bounded readdir API when available to avoid
    // being hung.
    let entries = fuchsia_fs::directory::readdir_recursive(inspect_proxy, /* timeout= */ None)
        .filter_map(|result| {
            async move {
                // TODO(fxbug.dev/49157): decide how to show directories that we failed to read.
                result.ok()
            }
        });
    let mut data_map = DataMap::new();
    pin_mut!(entries);
    // TODO(fxbug.dev/60250) convert this async loop to a stream so we can carry backpressure
    while let Some(entry) = entries.next().await {
        // We are only currently interested in inspect VMO files (root.inspect) and
        // inspect services.
        if let Ok(Some(proxy)) = maybe_load_service::<TreeMarker>(inspect_proxy, &entry) {
            data_map.insert(entry.name.into_boxed_str(), InspectData::Tree(proxy));
            continue;
        }

        if let Ok(Some(proxy)) = maybe_load_service::<InspectMarker>(inspect_proxy, &entry) {
            data_map.insert(entry.name.into_boxed_str(), InspectData::DeprecatedFidl(proxy));
            continue;
        }

        if !entry.name.ends_with(".inspect")
            || entry.kind != fuchsia_fs::directory::DirentKind::File
        {
            continue;
        }

        let file_proxy = match fuchsia_fs::open_file(
            inspect_proxy,
            Path::new(&entry.name),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        ) {
            Ok(proxy) => proxy,
            Err(_) => {
                // It should be ok to not be able to read a file. The file might be closed by the
                // time we get here.
                continue;
            }
        };

        // Obtain the backing vmo.
        let vmo = match file_proxy.get_backing_memory(fio::VmoFlags::READ).await {
            Ok(vmo) => vmo,
            Err(_) => {
                // It should be ok to not be able to read a file. The file might be closed by the
                // time we get here.
                continue;
            }
        };

        let data = match vmo.map_err(zx::Status::from_raw) {
            Ok(vmo) => InspectData::Vmo(vmo),
            Err(err) => {
                match err {
                    zx::Status::NOT_SUPPORTED => {}
                    err => {
                        error!(
                            file = %entry.name, ?err,
                            "unexpected error from GetBackingMemory",
                        )
                    }
                }
                match fuchsia_fs::file::read(&file_proxy).await {
                    Ok(contents) => InspectData::File(contents),
                    Err(_) => {
                        // It should be ok to not be able to read a file. The file might be closed
                        // by the time we get here.
                        continue;
                    }
                }
            }
        };
        data_map.insert(entry.name.into_boxed_str(), data);
    }

    data_map
}

/// Convert a fully-qualified path to a directory-proxy in the executing namespace.
#[cfg(test)]
pub async fn find_directory_proxy(
    path: &Path,
) -> Result<fio::DirectoryProxy, fuchsia_fs::node::OpenError> {
    fuchsia_fs::directory::open_in_namespace(
        &path.to_string_lossy(),
        fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
    )
}

#[cfg(test)]
pub fn collect(
    path: std::path::PathBuf,
) -> futures::future::BoxFuture<'static, Result<DataMap, anyhow::Error>> {
    async move {
        let inspect_proxy = match find_directory_proxy(&path).await {
            Ok(proxy) => proxy,
            Err(e) => {
                return Err(anyhow::format_err!(
                    "Failed to open out directory at {:?}: {}",
                    path,
                    e
                ));
            }
        };

        Ok(populate_data_map(&inspect_proxy).await)
    }
    .boxed()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fdio,
        fidl::endpoints::DiscoverableProtocolMarker,
        fidl_fuchsia_inspect::TreeMarker,
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::{assert_data_tree, reader, Inspector},
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::{FutureExt, StreamExt},
        std::path::PathBuf,
    };

    fn get_vmo(text: &[u8]) -> zx::Vmo {
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(text, 0).unwrap();
        vmo
    }

    #[fuchsia::test]
    async fn inspect_data_collector() {
        let path = PathBuf::from("/test-bindings");
        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = get_vmo(b"test1");
        let vmo2 = get_vmo(b"test2");
        let vmo3 = get_vmo(b"test3");
        let vmo4 = get_vmo(b"test4");
        fs.dir("diagnostics").add_vmo_file_at("root.inspect", vmo);
        fs.dir("diagnostics").add_vmo_file_at("root_not_inspect", vmo2);
        fs.dir("diagnostics").dir("a").add_vmo_file_at("root.inspect", vmo3);
        fs.dir("diagnostics").dir("b").add_vmo_file_at("root.inspect", vmo4);
        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::Task::spawn(fs.collect()).detach();

        let (done0, done1) = zx::Channel::create().unwrap();

        let thread_path = path.join("out/diagnostics");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::LocalExecutor::new().unwrap();

            executor.run_singlethreaded(async {
                let extra_data = collect(path).await.expect("collector missing data");
                assert_eq!(3, extra_data.len());

                let assert_extra_data = |path: &str, content: &[u8]| {
                    let extra = extra_data.get(path);
                    assert!(extra.is_some());

                    match extra.unwrap() {
                        InspectData::Vmo(vmo) => {
                            let mut buf = [0u8; 5];
                            vmo.read(&mut buf, 0).expect("reading vmo");
                            assert_eq!(content, &buf);
                        }
                        v => {
                            panic!("Expected Vmo, got {:?}", v);
                        }
                    }
                };

                assert_extra_data("root.inspect", b"test1");
                assert_extra_data("a/root.inspect", b"test3");
                assert_extra_data("b/root.inspect", b"test4");

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fuchsia::test]
    async fn inspect_data_collector_tree() {
        let path = PathBuf::from("/test-bindings2");

        // Make a ServiceFs serving an inspect tree.
        let mut fs = ServiceFs::new();
        let inspector = Inspector::new();
        inspector.root().record_int("a", 1);
        inspector.root().record_lazy_child("lazy", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_double("b", 3.25);
                Ok(inspector)
            }
            .boxed()
        });
        inspect_runtime::serve(&inspector, &mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::Task::spawn(fs.collect()).detach();

        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out/diagnostics");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::LocalExecutor::new().unwrap();

            executor.run_singlethreaded(async {
                let extra_data = collect(path).await.expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get(TreeMarker::PROTOCOL_NAME);
                assert!(extra.is_some());

                match extra.unwrap() {
                    InspectData::Tree(tree) => {
                        // Assert we can read the tree proxy and get the data we expected.
                        let hierarchy =
                            reader::read(tree).await.expect("failed to read hierarchy from tree");
                        assert_data_tree!(hierarchy, root: {
                            a: 1i64,
                            lazy: {
                                b: 3.25,
                            }
                        });
                    }
                    v => {
                        panic!("Expected Tree, got {:?}", v);
                    }
                }

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }
}

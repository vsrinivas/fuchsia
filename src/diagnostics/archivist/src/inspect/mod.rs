// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The collection module consists of methods and structures around two
//! different types of event streams: ComponentEvent and HubEvent.
//!
//! HubEvents are used internally for the HubCollector to attach watchers
//! for various different types in the Hub hierarchy. ComponentEvents are
//! exposed externally over a stream for use by the Archivist.

use {
    crate::collection,
    failure::{self, format_err, Error},
    fidl_fuchsia_io::NodeInfo,
    files_async,
    futures::future::BoxFuture,
    futures::FutureExt,
    io_util,
    std::path::PathBuf,
    std::sync::{Arc, Mutex},
};

/// InspectDataCollector holds the Inspect VMOs associated with a particular
/// component.
#[derive(Clone, Debug)]
pub struct InspectDataCollector {
    /// The inspect data associated with a particular event.
    ///
    /// This is wrapped in an Arc Mutex so it can be shared between multiple data sources.
    ///
    /// Note: The Arc is needed so that we can both add the data map to a data collector
    ///       and trigger async collection of the data in the same method. This can only
    ///       be done by allowing the async method to populate the same data that is being
    ///       passed into the component event.
    inspect_data_map: Arc<Mutex<Option<collection::DataMap>>>,
}

impl InspectDataCollector {
    /// Construct a new InspectDataCollector, wrapped by an Arc<Mutex>.
    pub fn new() -> Self {
        InspectDataCollector {
            inspect_data_map: Arc::new(Mutex::new(Some(collection::DataMap::new()))),
        }
    }

    /// Adds a key value to the contained vector if it hasn't been taken yet. Otherwise, does
    /// nothing.
    fn maybe_add(&mut self, key: String, value: collection::Data) {
        let mut data_map = self.inspect_data_map.lock().unwrap();
        match data_map.as_mut() {
            Some(map) => {
                map.insert(key, value);
            }
            _ => {}
        };
    }
}

impl collection::DataCollector for InspectDataCollector {
    /// Takes the contained extra data. Additions following this have no effect.
    fn take_data(self: Box<Self>) -> Option<collection::DataMap> {
        self.inspect_data_map.lock().unwrap().take()
    }

    /// Collect extra data stored under the given path.
    ///
    /// This currently only does a single pass over the directory to find information.
    fn collect(mut self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            let proxy = match io_util::open_directory_in_namespace(
                &path.to_string_lossy(),
                io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
            ) {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!("Failed to open out directory at {:?}: {}", path, e));
                }
            };

            for entry in files_async::readdir_recursive(&proxy).await?.into_iter() {
                // We are only currently interested in inspect files.
                if !entry.name.ends_with(".inspect") || entry.kind != files_async::DirentKind::File
                {
                    continue;
                }

                let path = path.join(entry.name);
                let proxy = match io_util::open_file_in_namespace(
                    &path.to_string_lossy(),
                    io_util::OPEN_RIGHT_READABLE,
                ) {
                    Ok(proxy) => proxy,
                    Err(_) => {
                        continue;
                    }
                };

                // Obtain the vmo backing any VmoFiles.
                match proxy.describe().await {
                    Ok(nodeinfo) => match nodeinfo {
                        NodeInfo::Vmofile(vmofile) => {
                            self.maybe_add(
                                path.file_name().unwrap().to_string_lossy().to_string(),
                                collection::Data::Vmo(vmofile.vmo),
                            );
                        }
                        _ => {}
                    },
                    Err(_) => {}
                }
            }

            Ok(())
        }
            .boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::collection::DataCollector, fdio, fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs, fuchsia_zircon as zx, fuchsia_zircon::Peered,
        futures::StreamExt,
    };

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_collector() {
        let path = PathBuf::from("/test-bindings");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(b"test", 0).unwrap();
        let vmo2 = zx::Vmo::create(4096).unwrap();
        vmo2.write(b"test", 0).unwrap();
        fs.dir("objects").add_vmo_file_at("root.inspect", vmo, 0, 4096);
        fs.dir("objects").add_vmo_file_at("root_not_inspect", vmo2, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();

        let thread_path = path.join("out");
        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = InspectDataCollector::new();

                // Trigger collection on a clone of the inspect collector so
                // we can use collector to take the collected data.
                Box::new(collector.clone()).collect(path).await.unwrap();

                let extra_data = Box::new(collector).take_data().expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get("root.inspect");
                assert!(extra.is_some());

                match extra.unwrap() {
                    collection::Data::Vmo(vmo) => {
                        let mut buf = [0u8; 4];
                        vmo.read(&mut buf, 0).expect("reading vmo");
                        assert_eq!(b"test", &buf);
                    }
                    v => {
                        panic!("Expected Vmo, got {:?}", v);
                    }
                }

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }
}

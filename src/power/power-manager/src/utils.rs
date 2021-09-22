// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Logs an error message if the passed in `result` is an error.
#[macro_export]
macro_rules! log_if_err {
    ($result:expr, $log_prefix:expr) => {
        if let Err(e) = $result.as_ref() {
            log::error!("{}: {}", $log_prefix, e);
        }
    };
}

/// Logs an error message if the provided `cond` evaluates to false. Also passes the same expression
/// and message into `debug_assert!`, which will panic if debug assertions are enabled.
#[macro_export]
macro_rules! log_if_false_and_debug_assert {
    ($cond:expr, $msg:expr) => {
        if !($cond) {
            log::error!($msg);
            debug_assert!($cond, $msg);
        }
    };
    ($cond:expr, $fmt:expr, $($arg:tt)+) => {
        if !($cond) {
            log::error!($fmt, $($arg)+);
            debug_assert!($cond, $fmt, $($arg)+);
        }
    };
}

#[cfg(test)]
mod log_err_with_debug_assert_tests {
    use crate::log_if_false_and_debug_assert;

    /// Tests that `log_if_false_and_debug_assert` panics for a false expression when debug
    /// assertions are enabled.
    #[test]
    #[should_panic(expected = "this will panic")]
    #[cfg(debug_assertions)]
    fn test_debug_assert() {
        log_if_false_and_debug_assert!(true, "this will not panic");
        log_if_false_and_debug_assert!(false, "this will panic");
    }

    /// Tests that `log_if_false_and_debug_assert` does not panic for a false expression when debug
    /// assertions are not enabled.
    #[test]
    #[cfg(not(debug_assertions))]
    fn test_non_debug_assert() {
        log_if_false_and_debug_assert!(true, "this will not panic");
        log_if_false_and_debug_assert!(false, "this will not panic either");
    }
}

/// Export the `connect_to_driver` function to be used throughout the crate.
pub use connect_to_driver::connect_to_driver;
mod connect_to_driver {
    use anyhow::{format_err, Error};
    use fidl::endpoints::Proxy as _;
    use fidl_fuchsia_io::{NodeProxy, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};

    /// Returns a NodeProxy opened at `path`. The path is guaranteed to exist before the connection
    /// is opened.
    async fn connect_channel(path: &str) -> Result<NodeProxy, Error> {
        device_watcher::recursive_wait_and_open_node(
            &io_util::open_directory_in_namespace(
                "/dev",
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            )?,
            path,
        )
        .await
    }

    /// Connects to the driver at `path`, returning a proxy of the specified type. The path is
    /// guaranteed to exist before the connection is opened.
    ///
    /// TODO(fxbug.dev/81378): factor this function out to a common library
    pub async fn connect_to_driver<T: fidl::endpoints::ProtocolMarker>(
        path: &str,
    ) -> Result<T::Proxy, Error> {
        match path.strip_prefix("/dev/") {
            Some(path) => fidl::endpoints::ClientEnd::<T>::new(
                connect_channel(path)
                    .await?
                    .into_channel()
                    .map_err(|_| format_err!("into_channel failed on NodeProxy"))?
                    .into_zx_channel(),
            )
            .into_proxy()
            .map_err(Into::into),
            None => Err(format_err!("Path must start with /dev/")),
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use async_utils::PollExt as _;
        use fidl::endpoints::{create_proxy, Proxy, ServerEnd};
        use fidl_fuchsia_io::{
            DirectoryMarker, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        };
        use fuchsia_async as fasync;
        use futures::TryStreamExt as _;
        use std::sync::Arc;
        use vfs::{
            directory::entry::DirectoryEntry, directory::helper::DirectlyMutable,
            execution_scope::ExecutionScope, file::vmo::read_only_static, pseudo_directory,
        };

        fn bind_to_dev(dir: Arc<dyn DirectoryEntry>) {
            let (dir_proxy, dir_server) = create_proxy::<DirectoryMarker>().unwrap();
            let scope = ExecutionScope::new();
            dir.open(
                scope,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                vfs::path::Path::dot(),
                ServerEnd::new(dir_server.into_channel()),
            );
            let ns = fdio::Namespace::installed().unwrap();
            ns.bind("/dev", dir_proxy.into_channel().unwrap().into_zx_channel()).unwrap();
        }

        /// Tests that `connect_to_driver` returns success for the valid existing path.
        #[fasync::run_singlethreaded(test)]
        async fn test_connect_to_driver_success() {
            bind_to_dev(pseudo_directory! {
                "class" => pseudo_directory!{
                    "thermal" => pseudo_directory! {
                        "000" => read_only_static("string beans")
                    }
                }
            });

            connect_to_driver::<NodeMarker>("/dev/class/thermal/000").await.unwrap();
        }

        /// Tests that `connect_to_driver` doesn't return until the required path is added.
        #[test]
        fn test_connect_to_driver_late_add() {
            let mut executor = fasync::TestExecutor::new().unwrap();

            let thermal_dir = pseudo_directory! {
                "000" => read_only_static("string cheese (cheddar)")
            };
            bind_to_dev(pseudo_directory! {
                "class" => pseudo_directory!{
                    "thermal" => thermal_dir.clone()
                }
            });

            let connect_future =
                &mut Box::pin(connect_to_driver::<NodeMarker>("/dev/class/thermal/001"));

            // The required path is initially not present
            assert!(executor.run_until_stalled(connect_future).is_pending());

            // Add the required path
            thermal_dir.add_entry("001", read_only_static("string cheese (mozzarella)")).unwrap();

            // Verify the wait future now returns successfully
            assert!(executor.run_until_stalled(connect_future).unwrap().is_ok());
        }

        /// Tests that `connect_to_driver` correctly waits even if the required parent directory
        /// does not yet exist.
        #[test]
        fn test_connect_to_driver_nonexistent_parent_dir() {
            let mut executor = fasync::TestExecutor::new().unwrap();

            bind_to_dev(pseudo_directory! {
                "class" => pseudo_directory!{
                    "cpu" => pseudo_directory! {
                        "000" => read_only_static("shoestring fries")
                    }
                }
            });

            assert!(executor
                .run_until_stalled(&mut Box::pin(connect_to_driver::<NodeMarker>(
                    "/dev/class/thermal/000"
                )))
                .is_pending());
        }

        /// Tests that the proxy returned by `connect_to_driver` is usable for sending a FIDL
        /// request.
        #[fasync::run_singlethreaded(test)]
        async fn test_connect_to_driver_gives_usable_proxy() {
            use fidl_fuchsia_device as fdev;

            // Create a pseudo directory with a fuchsia.device.Controller FIDL server hosted at
            // class/fake_dev_controller and bind it to our /dev
            bind_to_dev(pseudo_directory! {
                "class" => pseudo_directory! {
                    "fake_dev_controller" => vfs::service::host(
                        move |mut stream: fdev::ControllerRequestStream| {
                            async move {
                                match stream.try_next().await.unwrap() {
                                    Some(fdev::ControllerRequest::GetCurrentPerformanceState {
                                        responder
                                    }) => {
                                        let _ = responder.send(8);
                                    }
                                    e => panic!("Unexpected request: {:?}", e),
                                }
                            }
                        }
                    )
                }
            });

            // Connect to the driver and call GetCurrentPerformanceState on it
            let result = crate::utils::connect_to_driver::<fdev::ControllerMarker>(
                "/dev/class/fake_dev_controller",
            )
            .await
            .expect("Failed to connect to driver")
            .get_current_performance_state()
            .await
            .expect("get_current_performance_state FIDL failed");

            // Verify we receive the expected result
            assert_eq!(result, 8);
        }

        /// Verifies that invalid arguments are rejected while valid ones are accepted.
        #[fasync::run_singlethreaded(test)]
        async fn test_connect_to_driver_valid_path() {
            bind_to_dev(pseudo_directory! {
                "class" => pseudo_directory!{
                    "cpu" => pseudo_directory! {
                        "000" => read_only_static("stringtown population 1")
                    }
                }
            });

            connect_to_driver::<NodeMarker>("/svc/fake_service").await.unwrap_err();
            connect_to_driver::<NodeMarker>("/dev/class/cpu/000").await.unwrap();
        }
    }
}

/// The number of nanoseconds since the system was powered on.
pub fn get_current_timestamp() -> crate::types::Nanoseconds {
    crate::types::Nanoseconds(fuchsia_async::Time::now().into_nanos())
}

use fidl_fuchsia_cobalt::HistogramBucket;

/// Convenient wrapper for creating and storing an integer histogram to use with Cobalt.
pub struct CobaltIntHistogram {
    /// Underlying histogram data storage.
    data: Vec<HistogramBucket>,

    /// Number of data values that have been added to the histogram.
    data_count: u32,

    /// Histogram configuration parameters.
    config: CobaltIntHistogramConfig,
}

/// Histogram configuration parameters used by CobaltIntHistogram.
pub struct CobaltIntHistogramConfig {
    pub floor: i64,
    pub num_buckets: u32,
    pub step_size: u32,
}

impl CobaltIntHistogram {
    /// Create a new CobaltIntHistogram.
    pub fn new(config: CobaltIntHistogramConfig) -> Self {
        Self { data: Self::new_vec(config.num_buckets), data_count: 0, config }
    }

    /// Create a new Vec<HistogramBucket> that represents the underlying histogram storage. Two
    /// extra buckets are added for underflow and overflow.
    fn new_vec(num_buckets: u32) -> Vec<HistogramBucket> {
        (0..num_buckets + 2).map(|i| HistogramBucket { index: i, count: 0 }).collect()
    }

    /// Add a data value to the histogram.
    pub fn add_data(&mut self, n: i64) {
        // Add one to index to account for underflow bucket at index 0
        let mut index = 1 + (n - self.config.floor) / self.config.step_size as i64;

        // Clamp index to 0 and self.data.len() - 1, which Cobalt uses for underflow and overflow,
        // respectively
        index = num_traits::clamp(index, 0, self.data.len() as i64 - 1);

        self.data[index as usize].count += 1;
        self.data_count += 1;
    }

    /// Get the number of data elements that have been added to the histogram.
    pub fn count(&self) -> u32 {
        self.data_count
    }

    /// Clear the histogram.
    pub fn clear(&mut self) {
        self.data = Self::new_vec(self.config.num_buckets);
        self.data_count = 0;
    }

    /// Get the underlying Vec<HistogramBucket> of the histogram.
    pub fn get_data(&self) -> Vec<HistogramBucket> {
        self.data.clone()
    }
}

// Finds all of the node config files under the test package's "/config/data" directory. The node
// config files are identified by a suffix of "node_config.json". The function then calls the
// provided `test_config_file` function for each found config file, passing the JSON structure in as
// an argument. The function returns success if each call to `test_config_file` succeeds. Otherwise,
// the first error encountered is returned.
#[cfg(test)]
pub fn test_each_node_config_file(
    test_config_file: impl Fn(&Vec<serde_json::Value>) -> Result<(), anyhow::Error>,
) -> Result<(), anyhow::Error> {
    use anyhow::Context as _;
    use serde_json as json;
    use std::fs;
    use std::fs::File;
    use std::io::BufReader;

    let config_files = fs::read_dir("/config/data")
        .unwrap()
        .filter(|f| f.as_ref().unwrap().file_name().to_str().unwrap().ends_with("node_config.json"))
        .map(|f| {
            let path = f.unwrap().path();
            let file_path = path.to_str().unwrap().to_string();
            let json = json::from_reader(BufReader::new(File::open(path).unwrap())).unwrap();
            (file_path, json)
        })
        .collect::<Vec<_>>();
    assert!(config_files.len() > 0, "No config files found");

    for (file_path, config_file) in config_files {
        test_config_file(&config_file).context(format!("Failed for file {}", file_path))?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// CobaltIntHistogram: tests that data added to the CobaltIntHistogram is correctly counted and
    /// bucketed.
    #[test]
    fn test_cobalt_histogram_data() {
        // Create the histogram and verify initial data count is 0
        let mut hist = CobaltIntHistogram::new(CobaltIntHistogramConfig {
            floor: 50,
            step_size: 10,
            num_buckets: 3,
        });
        assert_eq!(hist.count(), 0);

        // Add some arbitrary values, making sure some do not land on the bucket boundary to further
        // verify the bucketing logic
        hist.add_data(50);
        hist.add_data(65);
        hist.add_data(75);
        hist.add_data(79);

        // Verify the values were counted and bucketed properly
        assert_eq!(hist.count(), 4);
        assert_eq!(
            hist.get_data(),
            vec![
                HistogramBucket { index: 0, count: 0 }, // underflow
                HistogramBucket { index: 1, count: 1 },
                HistogramBucket { index: 2, count: 1 },
                HistogramBucket { index: 3, count: 2 },
                HistogramBucket { index: 4, count: 0 } // overflow
            ]
        );

        // Verify `clear` works as expected
        hist.clear();
        assert_eq!(hist.count(), 0);
        assert_eq!(
            hist.get_data(),
            vec![
                HistogramBucket { index: 0, count: 0 }, // underflow
                HistogramBucket { index: 1, count: 0 },
                HistogramBucket { index: 2, count: 0 },
                HistogramBucket { index: 3, count: 0 },
                HistogramBucket { index: 4, count: 0 }, // overflow
            ]
        );
    }

    /// CobaltIntHistogram: tests that invalid data values are logged in the correct
    /// underflow/overflow buckets.
    #[test]
    fn test_cobalt_histogram_invalid_data() {
        let mut hist = CobaltIntHistogram::new(CobaltIntHistogramConfig {
            floor: 0,
            step_size: 1,
            num_buckets: 2,
        });

        hist.add_data(-2);
        hist.add_data(-1);
        hist.add_data(0);
        hist.add_data(1);
        hist.add_data(2);

        assert_eq!(
            hist.get_data(),
            vec![
                HistogramBucket { index: 0, count: 2 }, // underflow
                HistogramBucket { index: 1, count: 1 },
                HistogramBucket { index: 2, count: 1 },
                HistogramBucket { index: 3, count: 1 } // overflow
            ]
        );
    }

    /// Tests that the `get_current_timestamp` function returns the expected current timestamp.
    #[test]
    fn test_get_current_timestamp() {
        use crate::types::Nanoseconds;

        let exec = fuchsia_async::TestExecutor::new_with_fake_time().unwrap();

        exec.set_fake_time(fuchsia_async::Time::from_nanos(0));
        assert_eq!(get_current_timestamp(), Nanoseconds(0));

        exec.set_fake_time(fuchsia_async::Time::from_nanos(1000));
        assert_eq!(get_current_timestamp(), Nanoseconds(1000));
    }
}

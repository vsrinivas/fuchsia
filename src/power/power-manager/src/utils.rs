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

/// Waits for a file at the given path to exist using fdio Watcher APIs. The provided `path` is
/// split into a directory path and a file name, then a watcher is set up on the directory path. If
/// the directory path itself does not exist, then this function is called recursively to wait for
/// it to be created as well.
fn wait_for_path(path: &std::path::Path) -> Result<(), anyhow::Error> {
    use {anyhow::format_err, fuchsia_zircon as zx};

    let svc_dir = path.parent().ok_or(format_err!("Invalid service path"))?;
    let svc_name = path.file_name().ok_or(format_err!("Invalid service name"))?;
    match fdio::watch_directory(
        &std::fs::File::open(&svc_dir)
            .map_err(|e| anyhow::format_err!("Failed to open service path: {}", e))?,
        zx::sys::ZX_TIME_INFINITE,
        |_event, found| if found == svc_name { Err(zx::Status::STOP) } else { Ok(()) },
    ) {
        zx::Status::STOP => Ok(()),
        zx::Status::PEER_CLOSED => wait_for_path(svc_dir),
        e => Err(format_err!(
            "Failed to find {:?} at path {} (watch_directory result = {})",
            svc_name,
            svc_dir.display(),
            e
        )),
    }
}

/// Creates a FIDL proxy and connects its ServerEnd to the driver at the specified path.
pub async fn connect_to_driver<T: fidl::endpoints::ProtocolMarker>(
    path: &String,
) -> Result<T::Proxy, anyhow::Error> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<T>()?;
    connect_channel_to_driver(server_end, path).await?;
    Ok(proxy)
}

/// Connects a ServerEnd to the driver at the specified path. Calls `wait_for_path` to ensure the
/// path exists before attempting a connection.
pub async fn connect_channel_to_driver<T: fidl::endpoints::ProtocolMarker>(
    server_end: fidl::endpoints::ServerEnd<T>,
    path: &String,
) -> Result<(), anyhow::Error> {
    // Verify the path exists before attempting to connect to it. We do this because when connecting
    // to drivers, a connection to a missing driver path would succeed but calls to it would fail.
    // So instead of requiring us to implement logic at a higher layer to poll repeatedly until a
    // driver is present, just verify the path exists here using the appropriate watcher APIs.
    let path_clone = path.clone();
    fuchsia_async::unblock(move || wait_for_path(&std::path::Path::new(&path_clone))).await?;

    fdio::service_connect(path, server_end.into_channel())
        .map_err(|s| anyhow::format_err!("Failed to connect to driver at {}: {}", path, s))?;

    Ok(())
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
        .filter(|f| {
            f.as_ref().unwrap().file_name().into_string().unwrap().ends_with("node_config.json")
        })
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

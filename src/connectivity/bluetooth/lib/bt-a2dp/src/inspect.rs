// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, NumericProperty, Property},
    fuchsia_inspect_contrib::nodes::NodeExt,
    fuchsia_inspect_derive::Inspect,
    fuchsia_zircon as zx,
};

/// An inspect node that represents a stream of data, recording the total amount of data
/// transferred and an instantaneous rate.
#[derive(Inspect, Default)]
pub struct DataStreamInspect {
    /// The total number of bytes transferred in this stream
    total_bytes: inspect::UintProperty,
    /// Bytes per second, based on the most recent update.
    bytes_per_second_current: inspect::UintProperty,
    /// Time that this stream started.
    /// Managed manually.
    #[inspect(skip)]
    start_time: Option<fuchsia_inspect_contrib::nodes::TimeProperty>,
    /// Used to calculate instantaneous bytes_per_second.
    #[inspect(skip)]
    last_update_time: Option<fasync::Time>,
    inspect_node: inspect::Node,
}

impl DataStreamInspect {
    pub fn start(&mut self) {
        if let Some(prop) = &self.start_time {
            prop.update();
        } else {
            self.start_time =
                Some(self.inspect_node.create_time_at("start_time", fasync::Time::now().into()));
        }
        self.last_update_time = Some(fasync::Time::now());
    }

    /// Record that `bytes` have been transferred as of `at`.
    /// This is recorded since the last time `transferred` or since `start` if it
    /// has never been called.
    /// Does nothing if this stream has never been started.
    pub fn record_transferred(&mut self, bytes: usize, at: fasync::Time) {
        let elapsed = match self.last_update_time.take() {
            None => return,
            Some(last) => at - last,
        };
        self.last_update_time = Some(at);
        self.total_bytes.add(bytes as u64);
        // NOTE: probably a better way to calculate the speed than using floats.
        let bytes_per_milli = bytes as f64 / elapsed.into_millis() as f64;
        let bytes_per_second = zx::Duration::from_seconds(1).into_millis() as f64 * bytes_per_milli;
        self.bytes_per_second_current.set(bytes_per_second as u64);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::DurationExt;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;
    use fuchsia_zircon::DurationNum;

    #[test]
    fn inspectable() {
        let exec = fasync::Executor::new_with_fake_time().expect("creating an executor");
        exec.set_fake_time(fasync::Time::from_nanos(5_678900000));
        let inspector = inspect::component::inspector();
        let root = inspector.root();

        let mut d =
            DataStreamInspect::default().with_inspect(root, "data_stream").expect("attach to tree");

        d.start();

        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 0 as u64,
                bytes_per_second_current: 0 as u64,
            }
        });

        // A half second passes.
        exec.set_fake_time(500.millis().after_now());

        // If we transferred 500 bytes then, we should have 1000 bytes per second.
        d.record_transferred(500, fasync::Time::now());

        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 500 as u64,
                bytes_per_second_current: 1000 as u64,
            }
        });

        // in 5 seconds, we transfer 500 more bytesm which is much slower.
        d.record_transferred(500, 5.seconds().after_now());

        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 1000 as u64,
                bytes_per_second_current: 100 as u64,
            }
        });
    }
}

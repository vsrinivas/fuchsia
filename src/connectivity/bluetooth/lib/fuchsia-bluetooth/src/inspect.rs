// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, NumericProperty, Property},
    fuchsia_inspect_contrib::nodes::{ManagedNode, NodeExt},
    fuchsia_inspect_derive::Inspect,
    fuchsia_zircon as zx,
    std::fmt,
};

const FALSE_VALUE: u64 = 0;
const TRUE_VALUE: u64 = 1;

/// Convert a type to the correct supported Inspect Property type. This is used in Bluetooth to
/// ensure consistent representations of values in Inspect.
///
/// Note: It represents them appropriately for Bluetooth but may not be the appropriate type
/// for other use cases. It shouldn't be used outside of the Bluetooth project.
pub trait ToProperty {
    type PropertyType;
    fn to_property(&self) -> Self::PropertyType;
}

impl ToProperty for bool {
    type PropertyType = u64;
    fn to_property(&self) -> Self::PropertyType {
        if *self {
            TRUE_VALUE
        } else {
            FALSE_VALUE
        }
    }
}

impl ToProperty for Option<bool> {
    type PropertyType = u64;
    fn to_property(&self) -> Self::PropertyType {
        self.as_ref().map(bool::to_property).unwrap_or(FALSE_VALUE)
    }
}

impl ToProperty for String {
    type PropertyType = String;
    fn to_property(&self) -> Self::PropertyType {
        self.to_string()
    }
}

impl<T, V> ToProperty for Vec<T>
where
    T: ToProperty<PropertyType = V>,
    V: ToString,
{
    type PropertyType = String;
    fn to_property(&self) -> Self::PropertyType {
        self.iter()
            .map(|t| <T as ToProperty>::to_property(t).to_string())
            .collect::<Vec<String>>()
            .join(", ")
    }
}

/// Vectors of T show up as a comma separated list string property. `None` types are
/// represented as an empty string.
impl<T, V> ToProperty for Option<Vec<T>>
where
    T: ToProperty<PropertyType = V>,
    V: ToString,
{
    type PropertyType = String;
    fn to_property(&self) -> Self::PropertyType {
        self.as_ref().map(ToProperty::to_property).unwrap_or_else(String::new)
    }
}

/// Convenience function to create a string containing the debug representation of an object that
/// implements `Debug`
pub trait DebugExt {
    fn debug(&self) -> String;
}

impl<T: fmt::Debug> DebugExt for T {
    fn debug(&self) -> String {
        format!("{:?}", self)
    }
}

/// Represents inspect data that is tied to a specific object. This inspect data and the object of
/// type T should always be bundled together.
pub trait InspectData<T> {
    fn new(object: &T, inspect: inspect::Node) -> Self;
}

pub trait IsInspectable
where
    Self: Sized + Send + Sync + 'static,
{
    type I: InspectData<Self>;
}

/// A wrapper around a type T that bundles some inspect data alongside instances of the type.
#[derive(Debug)]
pub struct Inspectable<T: IsInspectable> {
    pub(crate) inner: T,
    pub(crate) inspect: T::I,
}

impl<T: IsInspectable> Inspectable<T> {
    /// Create a new instance of an `Inspectable` wrapper type containing the T instance that
    /// it wraps along with populated inspect data.
    pub fn new(object: T, inspect: inspect::Node) -> Inspectable<T> {
        Inspectable { inspect: T::I::new(&object, inspect), inner: object }
    }
}

/// `Inspectable`s can always safely be immutably dereferenced as the type T that they wrap
/// because the data will not be mutated through this reference.
impl<T: IsInspectable> std::ops::Deref for Inspectable<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

/// A trait representing the inspect data for a type T that will never be mutated. This trait
/// allows for a simpler "fire and forget" representation of the inspect data associated with an
/// object. This is because inspect handles for the data will never need to be accessed after
/// creation.
pub trait ImmutableDataInspect<T> {
    fn new(data: &T, manager: ManagedNode) -> Self;
}

/// "Fire and forget" representation of some inspect data that does not allow access inspect
/// handles after they are created.
pub struct ImmutableDataInspectManager {
    pub(crate) _manager: ManagedNode,
}

impl<T, I: ImmutableDataInspect<T>> InspectData<T> for I {
    /// Create a new instance of some type `I` that represents the immutable inspect data for a type
    /// `T`. This is done by handing `I` a `ManagedNode` instead of a `Node` and calling into the
    /// monomorphized version of ImmutableDataInspect<T> for I.
    fn new(data: &T, inspect: inspect::Node) -> I {
        I::new(data, ManagedNode::new(inspect))
    }
}

/// The values associated with a data transfer.
struct DataTransferStats {
    /// The time at which the data transfer was recorded.
    time: fasync::Time,
    /// The elapsed amount of time (nanos) the data transfer took place over.
    elapsed: std::num::NonZeroU64,
    /// The bytes transferred.
    bytes: usize,
}

impl DataTransferStats {
    /// Calculates and returns the throughput of the `bytes` received in the
    /// data transfer.
    fn calculate_throughput(&self) -> u64 {
        // NOTE: probably a better way to calculate the speed than using floats.
        let bytes_per_nano = self.bytes as f64 / self.elapsed.get() as f64;
        let bytes_per_second = zx::Duration::from_seconds(1).into_nanos() as f64 * bytes_per_nano;
        bytes_per_second as u64
    }
}

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
    last_update: Option<DataTransferStats>,
    inspect_node: inspect::Node,
}

impl DataStreamInspect {
    pub fn start(&mut self) {
        let now = fasync::Time::now();
        if let Some(prop) = &self.start_time {
            prop.set_at(now.into());
        } else {
            self.start_time = Some(self.inspect_node.create_time_at("start_time", now.into()));
        }
        self.last_update = Some(DataTransferStats {
            time: now,
            elapsed: std::num::NonZeroU64::new(1).unwrap(), // Default smallest interval of 1 nano
            bytes: 0,
        });
    }

    /// Record that `bytes` have been transferred as of `at`.
    /// This is recorded since the `last_update` time or since `start` if it
    /// has never been called.
    /// Does nothing if this stream has never been started or if the provided `at` time
    /// is in the past relative to the `last_update` time.
    pub fn record_transferred(&mut self, bytes: usize, at: fasync::Time) {
        let (elapsed, current_bytes) = match self.last_update {
            Some(DataTransferStats { time: last, .. }) if at > last => {
                // A new data transfer - calculate the new elapsed time interval.
                let elapsed = (at - last).into_nanos() as u64;
                (std::num::NonZeroU64::new(elapsed).unwrap(), bytes)
            }
            Some(DataTransferStats { time: last, elapsed, bytes: last_bytes }) if at == last => {
                // An addition to the previous data transfer - use the previous elapsed time
                // interval and an updated byte count.
                (elapsed, last_bytes + bytes)
            }
            _ => return, // Otherwise, we haven't started or received an invalid `at` time.
        };

        let transfer = DataTransferStats { time: at, elapsed, bytes: current_bytes };
        self.total_bytes.add(bytes as u64);
        self.bytes_per_second_current.set(transfer.calculate_throughput());
        self.last_update = Some(transfer);
    }
}

/// A placeholder node that can be used in tests that do not care about the `Node` value
pub fn placeholder_node() -> fuchsia_inspect::Node {
    fuchsia_inspect::Inspector::new().root().create_child("placeholder")
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async::DurationExt;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;
    use fuchsia_zircon::DurationNum;

    #[test]
    fn bool_to_property() {
        let b = false.to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = true.to_property();
        assert_eq!(b, TRUE_VALUE);
    }

    #[test]
    fn optional_bool_to_property() {
        let b: u64 = None::<bool>.to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = Some(false).to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = Some(true).to_property();
        assert_eq!(b, TRUE_VALUE);
    }

    #[test]
    fn string_vec_to_property() {
        let s = Vec::<String>::new().to_property();
        assert_eq!(s, "");
        let s = vec!["foo".to_string()].to_property();
        assert_eq!(s, "foo");
        let s = vec!["foo".to_string(), "bar".to_string(), "baz".to_string()].to_property();
        assert_eq!(s, "foo, bar, baz");
    }

    #[test]
    fn optional_string_vec_to_property() {
        let s = Some(vec!["foo".to_string(), "bar".to_string(), "baz".to_string()]).to_property();
        assert_eq!(s, "foo, bar, baz");
    }

    #[test]
    fn debug_string() {
        #[derive(Debug)]
        struct Foo {
            bar: u8,
            baz: &'static str,
        }
        let foo = Foo { bar: 1, baz: "baz value" };
        assert_eq!(format!("{:?}", foo), foo.debug());
    }

    /// Sets up an inspect test with an executor at timestamp `curr_time`.
    fn setup_inspect(
        curr_time: i64,
    ) -> (fasync::Executor, fuchsia_inspect::Inspector, DataStreamInspect) {
        let exec = fasync::Executor::new_with_fake_time().expect("creating an executor");
        exec.set_fake_time(fasync::Time::from_nanos(curr_time));
        let inspector = fuchsia_inspect::Inspector::new();
        let d = DataStreamInspect::default()
            .with_inspect(inspector.root(), "data_stream")
            .expect("attach to tree");

        (exec, inspector, d)
    }

    #[test]
    fn data_stream_inspect_data_transfer_before_start_has_no_effect() {
        let (_exec, inspector, mut d) = setup_inspect(5_123400000);

        // Default inspect tree.
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                total_bytes: 0 as u64,
                bytes_per_second_current: 0 as u64,
            }
        });

        // Recording a data transfer before start() has no effect.
        d.record_transferred(1, fasync::Time::now());
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                total_bytes: 0 as u64,
                bytes_per_second_current: 0 as u64,
            }
        });
    }

    #[test]
    fn data_stream_inspect_record_past_time_has_no_effect() {
        let curr_time = 5_678900000;
        let (_exec, inspector, mut d) = setup_inspect(curr_time);

        d.start();
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 0 as u64,
                bytes_per_second_current: 0 as u64,
            }
        });

        // Recording a data transfer with an older time has no effect.
        let time_from_past = curr_time - 10;
        d.record_transferred(1, fasync::Time::from_nanos(time_from_past));
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 0 as u64,
                bytes_per_second_current: 0 as u64,
            }
        });
    }

    #[test]
    fn data_stream_inspect_data_transfer_immediately_after_start_is_ok() {
        let curr_time = 5_678900000;
        let (_exec, inspector, mut d) = setup_inspect(curr_time);

        d.start();
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 0 as u64,
                bytes_per_second_current: 0 as u64,
            }
        });

        // Although unlikely, recording a data transfer at the same instantaneous moment as starting
        // is OK.
        d.record_transferred(5, fasync::Time::from_nanos(curr_time));
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 5 as u64,
                bytes_per_second_current: 5_000_000_000 as u64,
            }
        });
    }

    #[test]
    fn data_stream_inspect_records_correct_throughput() {
        let (exec, inspector, mut d) = setup_inspect(5_678900000);

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

        // In 5 seconds, we transfer 500 more bytes which is much slower.
        exec.set_fake_time(5.seconds().after_now());
        d.record_transferred(500, fasync::Time::now());
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 1000 as u64,
                bytes_per_second_current: 100 as u64,
            }
        });

        // Receiving another update at the same time is OK.
        d.record_transferred(900, fasync::Time::now());
        assert_inspect_tree!(inspector, root: {
            data_stream: {
                start_time: 5_678900000i64,
                total_bytes: 1900 as u64,
                bytes_per_second_current: 280 as u64,
            }
        });
    }

    #[test]
    fn test_calculate_throughput() {
        let time = fasync::Time::from_nanos(1_000_000_000);
        // No throughput.
        let bytes = 0;
        let elapsed = std::num::NonZeroU64::new(1_000_000).unwrap();
        let transfer1 = DataTransferStats { time, elapsed, bytes };
        assert_eq!(transfer1.calculate_throughput(), 0);

        // Fractional throughput in bytes per nano.
        let bytes = 1;
        let elapsed = std::num::NonZeroU64::new(1_000_000).unwrap();
        let transfer2 = DataTransferStats { time, elapsed, bytes };
        assert_eq!(transfer2.calculate_throughput(), 1000);

        // Fractional throughput in bytes per nano.
        let bytes = 5;
        let elapsed = std::num::NonZeroU64::new(9_502_241).unwrap();
        let transfer3 = DataTransferStats { time, elapsed, bytes };
        let expected = 526; // Calculated using calculator.
        assert_eq!(transfer3.calculate_throughput(), expected);

        // Very small fractional throughput in bytes per nano. Should truncate to 0.
        let bytes = 19;
        let elapsed = std::num::NonZeroU64::new(5_213_999_642_004).unwrap();
        let transfer4 = DataTransferStats { time, elapsed, bytes };
        assert_eq!(transfer4.calculate_throughput(), 0);

        // Throughput of 1 in bytes per nano.
        let bytes = 100;
        let elapsed = std::num::NonZeroU64::new(100).unwrap();
        let transfer5 = DataTransferStats { time, elapsed, bytes };
        assert_eq!(transfer5.calculate_throughput(), 1_000_000_000);

        // Large throughput in bytes per nano.
        let bytes = 100;
        let elapsed = std::num::NonZeroU64::new(1).unwrap();
        let transfer6 = DataTransferStats { time, elapsed, bytes };
        assert_eq!(transfer6.calculate_throughput(), 100_000_000_000);

        // Large fractional throughput in bytes per nano.
        let bytes = 987_432_002_999;
        let elapsed = std::num::NonZeroU64::new(453).unwrap();
        let transfer7 = DataTransferStats { time, elapsed, bytes };
        let expected = 2_179_761_596_024_282_368; // Calculated using calculator.
        assert_eq!(transfer7.calculate_throughput(), expected);
    }
}

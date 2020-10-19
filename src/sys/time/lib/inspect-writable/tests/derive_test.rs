// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{assert_inspect_tree, Inspector, Node},
    inspect_writable::{InspectWritable, InspectWritableNode},
};

#[derive(Debug)]
enum Summary {
    Sunny,
    Raining,
}

#[derive(InspectWritable)]
struct WeatherReport {
    temp: i8,
    pressure: u16,
    precipitation: u64,
    summary: Summary,
}

const WEATHER_REPORT_1: WeatherReport =
    WeatherReport { temp: 22, pressure: 1013, precipitation: 0, summary: Summary::Sunny };

const WEATHER_REPORT_2: WeatherReport =
    WeatherReport { temp: -3, pressure: 987, precipitation: 100, summary: Summary::Raining };

#[test]
fn valid_struct() {
    let inspector = &Inspector::new();
    let root = inspector.root();
    // Use the record API to write the initial state one time.
    root.record_child("initial_state", |node| WEATHER_REPORT_1.record(node));
    // Use the create API to write the state using a wrapped node we could update later.
    let last_state = WEATHER_REPORT_1.create(root.create_child("last_state"));

    assert_inspect_tree!(
        inspector,
        root: contains {
            initial_state: contains {
                temp: WEATHER_REPORT_1.temp as i64,
                pressure: WEATHER_REPORT_1.pressure as u64,
                precipitation: WEATHER_REPORT_1.precipitation,
                summary: format!("{:?}", WEATHER_REPORT_1.summary),
            },
            last_state: contains {
                temp: WEATHER_REPORT_1.temp as i64,
                pressure: WEATHER_REPORT_1.pressure as u64,
                precipitation: WEATHER_REPORT_1.precipitation,
                summary: format!("{:?}", WEATHER_REPORT_1.summary),
            }
        }
    );

    // Update the last state to a new value.
    last_state.update(&WEATHER_REPORT_2);

    assert_inspect_tree!(
        inspector,
        root: contains {
            initial_state: contains {
                temp: WEATHER_REPORT_1.temp as i64,
                pressure: WEATHER_REPORT_1.pressure as u64,
                precipitation: WEATHER_REPORT_1.precipitation,
                summary: format!("{:?}", WEATHER_REPORT_1.summary),
            },
            last_state: contains {
                temp: WEATHER_REPORT_2.temp as i64,
                pressure: WEATHER_REPORT_2.pressure as u64,
                precipitation: WEATHER_REPORT_2.precipitation,
                summary: format!("{:?}", WEATHER_REPORT_2.summary),
            }
        }
    );
}

// TODO(jsankey): Add some additional tests to cover compile failures from applying
// #[derive(InspectWritable)] to an invalid struct.

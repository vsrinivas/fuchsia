// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    input_report::types::{
        InputDeviceMatchArgs, SerializableDeviceDescriptor, SerializableFeatureReport,
        SerializableInputReport,
    },
};
use anyhow::Error;
use fidl_fuchsia_input_report::{
    FeatureReport, InputDeviceMarker, InputDeviceProxy, InputReportsReaderMarker,
    InputReportsReaderProxy,
};
use fuchsia_syslog::macros::*;
use glob::glob;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};
use std::collections::HashMap;
use std::vec::Vec;

#[derive(Debug)]
struct InputDeviceConnection {
    proxy: InputDeviceProxy,
    reader: Option<InputReportsReaderProxy>,
}

impl InputDeviceConnection {
    pub fn new(proxy: InputDeviceProxy) -> InputDeviceConnection {
        Self { proxy: proxy, reader: None }
    }
}

#[derive(Debug)]
pub struct InputReportFacade {
    connections: RwLock<HashMap<InputDeviceMatchArgs, InputDeviceConnection>>,
}

fn connect_to_device(path: std::path::PathBuf) -> Option<InputDeviceProxy> {
    match fidl::endpoints::create_proxy::<InputDeviceMarker>() {
        Ok((proxy, server)) => {
            match fdio::service_connect(&path.to_string_lossy(), server.into_channel()) {
                Ok(_r) => Some(proxy),
                Err(_e) => None,
            }
        }
        Err(_e) => None,
    }
}

async fn check_device_match(
    proxy: InputDeviceProxy,
    match_args: &InputDeviceMatchArgs,
) -> Option<InputDeviceProxy> {
    match proxy.get_descriptor().await.ok().map(|desc| desc.device_info).flatten() {
        Some(info) => {
            // Accept the device if all specified match arguments are equal to the corresponding
            // fields in DeviceInfo.
            if match_args.vendor_id.unwrap_or(info.vendor_id) != info.vendor_id {
                None
            } else if match_args.product_id.unwrap_or(info.product_id) != info.product_id {
                None
            } else if match_args.version.unwrap_or(info.version) != info.version {
                None
            } else {
                Some(proxy)
            }
        }
        None => match_args
            .vendor_id
            .or(match_args.product_id)
            .or(match_args.version)
            // The device doesn't provide DeviceInfo -- only accept if if no match arguments were
            // specified.
            .map_or(Some(proxy), |_num| None),
    }
}

impl InputReportFacade {
    pub fn new() -> InputReportFacade {
        Self { connections: RwLock::new(HashMap::new()) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: InputDeviceProxy) -> Self {
        let mut connections = HashMap::<InputDeviceMatchArgs, InputDeviceConnection>::new();
        connections.insert(InputDeviceMatchArgs::default(), InputDeviceConnection::new(proxy));
        Self { connections: RwLock::new(connections) }
    }

    async fn get_proxy(
        &self,
        match_args: &InputDeviceMatchArgs,
    ) -> Result<InputDeviceProxy, Error> {
        let lock = self.connections.upgradable_read();
        if let Some(connection) = lock.get(&match_args) {
            return Ok(connection.proxy.clone());
        }

        let tag = "InputReportFacade::get_proxy";

        let mut devices = Vec::<InputDeviceProxy>::new();
        for proxy in
            glob("/dev/class/input-report/*")?.filter_map(Result::ok).filter_map(connect_to_device)
        {
            if let Some(p) = check_device_match(proxy, &match_args).await {
                devices.push(p);
            }
        }

        if devices.len() < 1 {
            fx_err_and_bail!(&with_line!(tag), "Failed to find matching input report device")
        } else if devices.len() > 1 {
            fx_err_and_bail!(&with_line!(tag), "Found multiple matching input report devices")
        } else {
            let proxy = devices.remove(0);
            RwLockUpgradableReadGuard::upgrade(lock)
                .insert(*match_args, InputDeviceConnection::new(proxy.clone()));
            Ok(proxy)
        }
    }

    async fn get_reader(
        &self,
        match_args: &InputDeviceMatchArgs,
    ) -> Result<InputReportsReaderProxy, Error> {
        let proxy = self.get_proxy(&match_args).await?;

        let mut lock = self.connections.write();
        // get_proxy should have created a corresponding connection for these match_args.
        let connection =
            lock.get_mut(&match_args).ok_or(format_err!("Failed to get input report proxy"))?;
        if let Some(reader) = &connection.reader {
            return Ok(reader.clone());
        }

        let (reader, server) = fidl::endpoints::create_proxy::<InputReportsReaderMarker>()?;
        proxy.get_input_reports_reader(server)?;
        connection.reader = Some(reader.clone());
        Ok(reader)
    }

    pub async fn get_reports(
        &self,
        match_args: InputDeviceMatchArgs,
    ) -> Result<Vec<SerializableInputReport>, Error> {
        match self.get_reader(&match_args).await?.read_input_reports().await? {
            Ok(r) => {
                let mut serializable_reports = Vec::<SerializableInputReport>::new();
                for report in r {
                    serializable_reports.push(SerializableInputReport::new(&report));
                }
                Ok(serializable_reports)
            }
            Err(e) => {
                let tag = "InputReportFacade::get_reports";
                fx_err_and_bail!(&with_line!(tag), format_err!("ReadInputReports failed: {:?}", e))
            }
        }
    }

    pub async fn get_descriptor(
        &self,
        match_args: InputDeviceMatchArgs,
    ) -> Result<SerializableDeviceDescriptor, Error> {
        let descriptor = self.get_proxy(&match_args).await?.get_descriptor().await?;
        Ok(SerializableDeviceDescriptor::new(&descriptor))
    }

    pub async fn get_feature_report(
        &self,
        match_args: InputDeviceMatchArgs,
    ) -> Result<SerializableFeatureReport, Error> {
        match self.get_proxy(&match_args).await?.get_feature_report().await? {
            Ok(r) => Ok(SerializableFeatureReport::new(&r)),
            Err(e) => {
                let tag = "InputReportFacade::get_feature_report";
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("GetFeaturereReport failed: {:?}", e)
                )
            }
        }
    }

    pub async fn set_feature_report(
        &self,
        match_args: InputDeviceMatchArgs,
        feature_report: FeatureReport,
    ) -> Result<(), Error> {
        match self.get_proxy(&match_args).await?.set_feature_report(feature_report).await? {
            Ok(()) => Ok(()),
            Err(e) => {
                let tag = "InputReportFacade::set_feature_report";
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("SetFeaturereReport failed: {:?}", e)
                )
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input_report::types::*;
    use fidl_fuchsia_input_report::*;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;
    use serde::de::Deserialize;
    use serde_json::{Map, Number, Value};

    type ExpectationCallback = Box<
        dyn FnOnce(
                InputDeviceRequest,
                &mut Option<fidl::endpoints::ServerEnd<InputReportsReaderMarker>>,
            ) + Send
            + 'static,
    >;

    type ReaderExpectationCallback = Box<dyn FnOnce(InputReportsReaderRequest) + Send + 'static>;

    struct MockInputReportBuilder {
        expected: Vec<ExpectationCallback>,
        reader_expected: Vec<ReaderExpectationCallback>,
    }

    impl MockInputReportBuilder {
        fn new() -> Self {
            Self { expected: vec![], reader_expected: vec![] }
        }

        fn expect_request(
            mut self,
            request: impl FnOnce(
                    InputDeviceRequest,
                    &mut Option<fidl::endpoints::ServerEnd<InputReportsReaderMarker>>,
                ) + Send
                + 'static,
        ) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_reader_request(
            mut self,
            request: impl FnOnce(InputReportsReaderRequest) + Send + 'static,
        ) -> Self {
            self.reader_expected.push(Box::new(request));
            self
        }

        fn expect_read_input_reports(self, reports: Vec<InputReport>) -> Self {
            self.expect_reader_request(move |req| match req {
                InputReportsReaderRequest::ReadInputReports { responder } => {
                    assert_matches!(responder.send(&mut Ok(reports)), Ok(()));
                }
            })
        }

        fn expect_get_input_reports_reader(self) -> Self {
            self.expect_request(move |req, reader| match req {
                InputDeviceRequest::GetInputReportsReader { reader: r, control_handle: _ } => {
                    reader.replace(r);
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_descriptor(self, descriptor: DeviceDescriptor) -> Self {
            self.expect_request(move |req, _reader| match req {
                InputDeviceRequest::GetDescriptor { responder } => {
                    assert_matches!(responder.send(descriptor), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_feature_report(self, feature_report: FeatureReport) -> Self {
            self.expect_request(move |req, _reader| match req {
                InputDeviceRequest::GetFeatureReport { responder } => {
                    assert_matches!(responder.send(&mut Ok(feature_report)), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_feature_report(self, feature_report: FeatureReport) -> Self {
            self.expect_request(move |req, _reader| match req {
                InputDeviceRequest::SetFeatureReport { report, responder } => {
                    assert_eq!(report, feature_report);
                    assert_matches!(responder.send(&mut Ok(())), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (InputReportFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<InputDeviceMarker>().unwrap();
            let fut = async move {
                let mut reader: Option<fidl::endpoints::ServerEnd<InputReportsReaderMarker>> = None;
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap(), &mut reader);
                }

                if self.reader_expected.len() > 0 {
                    assert!(reader.is_some());
                    let reader_result = reader.unwrap().into_stream();
                    assert!(reader_result.is_ok());

                    let mut reader_stream = reader_result.unwrap();
                    for expected in self.reader_expected {
                        expected(reader_stream.next().await.unwrap().unwrap());
                    }
                    assert_matches!(reader_stream.next().await, None);
                }

                assert_matches!(stream.next().await, None);
            };

            (InputReportFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_device_descriptor() {
        let (facade, expectations) = MockInputReportBuilder::new()
            .expect_get_descriptor(DeviceDescriptor {
                device_info: Some(DeviceInfo { vendor_id: 1, product_id: 2, version: 3 }),
                mouse: None,
                sensor: Some(SensorDescriptor {
                    input: Some(SensorInputDescriptor {
                        values: Some(vec![
                            SensorAxis {
                                axis: Axis {
                                    range: Range { min: -100, max: 100 },
                                    unit: Unit {
                                        type_: UnitType::SiLinearAcceleration,
                                        exponent: 0,
                                    },
                                },
                                type_: SensorType::AccelerometerX,
                            },
                            SensorAxis {
                                axis: Axis {
                                    range: Range { min: -10000, max: 10000 },
                                    unit: Unit { type_: UnitType::Webers, exponent: 0 },
                                },
                                type_: SensorType::MagnetometerX,
                            },
                            SensorAxis {
                                axis: Axis {
                                    range: Range { min: 0, max: 1000 },
                                    unit: Unit { type_: UnitType::Lux, exponent: 0 },
                                },
                                type_: SensorType::LightIlluminance,
                            },
                        ]),
                    }),
                    feature: Some(SensorFeatureDescriptor {
                        report_interval: Some(Axis {
                            range: Range { min: 0, max: 1000000000 },
                            unit: Unit { type_: UnitType::Seconds, exponent: 0 },
                        }),
                        supports_reporting_state: Some(true),
                        sensitivity: Some(vec![SensorAxis {
                            axis: Axis {
                                range: Range { min: 0, max: 3 },
                                unit: Unit { type_: UnitType::Lux, exponent: 0 },
                            },
                            type_: SensorType::LightIlluminance,
                        }]),
                        threshold_high: Some(vec![SensorAxis {
                            axis: Axis {
                                range: Range { min: 0, max: 0xfff },
                                unit: Unit { type_: UnitType::Lux, exponent: 0 },
                            },
                            type_: SensorType::LightIlluminance,
                        }]),
                        threshold_low: Some(vec![SensorAxis {
                            axis: Axis {
                                range: Range { min: 0, max: 0xfff },
                                unit: Unit { type_: UnitType::Lux, exponent: 0 },
                            },
                            type_: SensorType::LightIlluminance,
                        }]),
                    }),
                }),
                touch: Some(TouchDescriptor {
                    input: Some(TouchInputDescriptor {
                        contacts: Some(vec![
                            ContactInputDescriptor {
                                position_x: Some(Axis {
                                    range: Range { min: 0, max: 200 },
                                    unit: Unit { type_: UnitType::Meters, exponent: -6 },
                                }),
                                position_y: Some(Axis {
                                    range: Range { min: 0, max: 100 },
                                    unit: Unit { type_: UnitType::Meters, exponent: -6 },
                                }),
                                pressure: Some(Axis {
                                    range: Range { min: 0, max: 10 },
                                    unit: Unit { type_: UnitType::Pascals, exponent: -6 },
                                }),
                                contact_width: None,
                                contact_height: None,
                            },
                            ContactInputDescriptor {
                                position_x: Some(Axis {
                                    range: Range { min: 0, max: 200 },
                                    unit: Unit { type_: UnitType::Meters, exponent: -6 },
                                }),
                                position_y: Some(Axis {
                                    range: Range { min: 0, max: 100 },
                                    unit: Unit { type_: UnitType::Meters, exponent: -6 },
                                }),
                                pressure: Some(Axis {
                                    range: Range { min: 0, max: 10 },
                                    unit: Unit { type_: UnitType::Pascals, exponent: -6 },
                                }),
                                contact_width: None,
                                contact_height: None,
                            },
                        ]),
                        max_contacts: Some(2),
                        touch_type: Some(TouchType::Touchpad),
                        buttons: Some(vec![1, 2, 3]),
                    }),
                }),
                keyboard: None,
                consumer_control: None,
            })
            .build();

        let test = async move {
            let descriptor = facade.get_descriptor(InputDeviceMatchArgs::default()).await;
            assert!(descriptor.is_ok());
            assert_eq!(
                descriptor.unwrap(),
                SerializableDeviceDescriptor {
                    device_info: Some(SerializableDeviceInfo {
                        vendor_id: 1,
                        product_id: 2,
                        version: 3,
                    }),
                    sensor: Some(SerializableSensorDescriptor {
                        input: Some(SerializableSensorInputDescriptor {
                            values: Some(vec![
                                SerializableSensorAxis {
                                    axis: SerializableAxis {
                                        range: SerializableRange { min: -100, max: 100 },
                                        unit: SerializableUnit {
                                            type_: UnitType::SiLinearAcceleration.into_primitive(),
                                            exponent: 0
                                        },
                                    },
                                    type_: SensorType::AccelerometerX.into_primitive(),
                                },
                                SerializableSensorAxis {
                                    axis: SerializableAxis {
                                        range: SerializableRange { min: -10000, max: 10000 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Webers.into_primitive(),
                                            exponent: 0
                                        },
                                    },
                                    type_: SensorType::MagnetometerX.into_primitive(),
                                },
                                SerializableSensorAxis {
                                    axis: SerializableAxis {
                                        range: SerializableRange { min: 0, max: 1000 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Lux.into_primitive(),
                                            exponent: 0
                                        },
                                    },
                                    type_: SensorType::LightIlluminance.into_primitive(),
                                },
                            ])
                        }),
                        feature: Some(SerializableSensorFeatureDescriptor {
                            report_interval: Some(SerializableAxis {
                                range: SerializableRange { min: 0, max: 1000000000 },
                                unit: SerializableUnit {
                                    type_: UnitType::Seconds.into_primitive(),
                                    exponent: 0
                                },
                            }),
                            supports_reporting_state: Some(true),
                            sensitivity: Some(vec![SerializableSensorAxis {
                                axis: SerializableAxis {
                                    range: SerializableRange { min: 0, max: 3 },
                                    unit: SerializableUnit {
                                        type_: UnitType::Lux.into_primitive(),
                                        exponent: 0
                                    },
                                },
                                type_: SensorType::LightIlluminance.into_primitive(),
                            }]),
                            threshold_high: Some(vec![SerializableSensorAxis {
                                axis: SerializableAxis {
                                    range: SerializableRange { min: 0, max: 0xfff },
                                    unit: SerializableUnit {
                                        type_: UnitType::Lux.into_primitive(),
                                        exponent: 0
                                    },
                                },
                                type_: SensorType::LightIlluminance.into_primitive(),
                            }]),
                            threshold_low: Some(vec![SerializableSensorAxis {
                                axis: SerializableAxis {
                                    range: SerializableRange { min: 0, max: 0xfff },
                                    unit: SerializableUnit {
                                        type_: UnitType::Lux.into_primitive(),
                                        exponent: 0
                                    },
                                },
                                type_: SensorType::LightIlluminance.into_primitive(),
                            }]),
                        }),
                    }),
                    touch: Some(SerializableTouchDescriptor {
                        input: Some(SerializableTouchInputDescriptor {
                            contacts: Some(vec![
                                SerializableContactInputDescriptor {
                                    position_x: Some(SerializableAxis {
                                        range: SerializableRange { min: 0, max: 200 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Meters.into_primitive(),
                                            exponent: -6
                                        },
                                    }),
                                    position_y: Some(SerializableAxis {
                                        range: SerializableRange { min: 0, max: 100 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Meters.into_primitive(),
                                            exponent: -6
                                        },
                                    }),
                                    pressure: Some(SerializableAxis {
                                        range: SerializableRange { min: 0, max: 10 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Pascals.into_primitive(),
                                            exponent: -6
                                        },
                                    }),
                                    contact_width: None,
                                    contact_height: None,
                                },
                                SerializableContactInputDescriptor {
                                    position_x: Some(SerializableAxis {
                                        range: SerializableRange { min: 0, max: 200 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Meters.into_primitive(),
                                            exponent: -6
                                        },
                                    }),
                                    position_y: Some(SerializableAxis {
                                        range: SerializableRange { min: 0, max: 100 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Meters.into_primitive(),
                                            exponent: -6
                                        },
                                    }),
                                    pressure: Some(SerializableAxis {
                                        range: SerializableRange { min: 0, max: 10 },
                                        unit: SerializableUnit {
                                            type_: UnitType::Pascals.into_primitive(),
                                            exponent: -6
                                        },
                                    }),
                                    contact_width: None,
                                    contact_height: None,
                                },
                            ]),
                            max_contacts: Some(2),
                            touch_type: Some(TouchType::Touchpad.into_primitive()),
                            buttons: Some(vec![1, 2, 3]),
                        })
                    }),
                }
            );
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_reports() {
        let (facade, expectations) = MockInputReportBuilder::new()
            .expect_get_input_reports_reader()
            .expect_read_input_reports(vec![
                InputReport {
                    event_time: None,
                    mouse: None,
                    sensor: Some(SensorInputReport { values: Some(vec![1, 2, 3, 4, 5]) }),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![
                            ContactInputReport {
                                contact_id: Some(1),
                                position_x: Some(100),
                                position_y: Some(200),
                                pressure: Some(10),
                                contact_width: None,
                                contact_height: None,
                            },
                            ContactInputReport {
                                contact_id: Some(2),
                                position_x: Some(20),
                                position_y: Some(10),
                                pressure: Some(5),
                                contact_width: None,
                                contact_height: None,
                            },
                            ContactInputReport {
                                contact_id: Some(3),
                                position_x: Some(0),
                                position_y: Some(0),
                                pressure: Some(1),
                                contact_width: None,
                                contact_height: None,
                            },
                        ]),
                        pressed_buttons: Some(vec![4, 5, 6]),
                    }),
                    keyboard: None,
                    consumer_control: None,
                    trace_id: Some(1),
                },
                InputReport {
                    event_time: Some(1000),
                    mouse: None,
                    sensor: Some(SensorInputReport { values: Some(vec![6, 7, 8, 9, 10]) }),
                    touch: None,
                    keyboard: None,
                    consumer_control: None,
                    trace_id: Some(2),
                },
                InputReport {
                    event_time: Some(2000),
                    mouse: None,
                    sensor: None,
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![
                            ContactInputReport {
                                contact_id: Some(1),
                                position_x: Some(1000),
                                position_y: Some(2000),
                                pressure: Some(5),
                                contact_width: None,
                                contact_height: None,
                            },
                            ContactInputReport {
                                contact_id: Some(3),
                                position_x: Some(10),
                                position_y: Some(10),
                                pressure: Some(5),
                                contact_width: None,
                                contact_height: None,
                            },
                        ]),
                        pressed_buttons: Some(vec![1, 2, 3]),
                    }),
                    keyboard: None,
                    consumer_control: None,
                    trace_id: Some(3),
                },
            ])
            .build();

        let test = async move {
            let report = facade.get_reports(InputDeviceMatchArgs::default()).await;
            assert!(report.is_ok());
            assert_eq!(
                report.unwrap(),
                vec![
                    SerializableInputReport {
                        event_time: None,
                        sensor: Some(SerializableSensorInputReport {
                            values: Some(vec![1, 2, 3, 4, 5])
                        }),
                        touch: Some(SerializableTouchInputReport {
                            contacts: Some(vec![
                                SerializableContactInputReport {
                                    contact_id: Some(1),
                                    position_x: Some(100),
                                    position_y: Some(200),
                                    pressure: Some(10),
                                    contact_width: None,
                                    contact_height: None,
                                },
                                SerializableContactInputReport {
                                    contact_id: Some(2),
                                    position_x: Some(20),
                                    position_y: Some(10),
                                    pressure: Some(5),
                                    contact_width: None,
                                    contact_height: None,
                                },
                                SerializableContactInputReport {
                                    contact_id: Some(3),
                                    position_x: Some(0),
                                    position_y: Some(0),
                                    pressure: Some(1),
                                    contact_width: None,
                                    contact_height: None,
                                },
                            ]),
                            pressed_buttons: Some(vec![4, 5, 6]),
                        }),
                        trace_id: Some(1),
                    },
                    SerializableInputReport {
                        event_time: Some(1000),
                        sensor: Some(SerializableSensorInputReport {
                            values: Some(vec![6, 7, 8, 9, 10])
                        }),
                        touch: None,
                        trace_id: Some(2),
                    },
                    SerializableInputReport {
                        event_time: Some(2000),
                        sensor: None,
                        touch: Some(SerializableTouchInputReport {
                            contacts: Some(vec![
                                SerializableContactInputReport {
                                    contact_id: Some(1),
                                    position_x: Some(1000),
                                    position_y: Some(2000),
                                    pressure: Some(5),
                                    contact_width: None,
                                    contact_height: None,
                                },
                                SerializableContactInputReport {
                                    contact_id: Some(3),
                                    position_x: Some(10),
                                    position_y: Some(10),
                                    pressure: Some(5),
                                    contact_width: None,
                                    contact_height: None,
                                },
                            ]),
                            pressed_buttons: Some(vec![1, 2, 3]),
                        }),
                        trace_id: Some(3),
                    },
                ]
            );
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_feature_report() {
        let (facade, expectations) = MockInputReportBuilder::new()
            .expect_get_feature_report(FeatureReport {
                sensor: Some(SensorFeatureReport {
                    report_interval: Some(100),
                    reporting_state: Some(SensorReportingState::ReportAllEvents),
                    sensitivity: Some(vec![1, 2, 3]),
                    threshold_high: None,
                    threshold_low: Some(vec![4, 5]),
                }),
            })
            .build();
        let test = async move {
            let report = facade.get_feature_report(InputDeviceMatchArgs::default()).await;
            assert!(report.is_ok());
            assert_eq!(
                report.unwrap(),
                SerializableFeatureReport {
                    sensor: Some(SerializableSensorFeatureReport {
                        report_interval: Some(100),
                        reporting_state: Some(2),
                        sensitivity: Some(vec![1, 2, 3]),
                        threshold_high: None,
                        threshold_low: Some(vec![4, 5]),
                    })
                }
            );
        };
        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_feature_report() {
        let (facade, expectations) = MockInputReportBuilder::new()
            .expect_set_feature_report(FeatureReport {
                sensor: Some(SensorFeatureReport {
                    report_interval: None,
                    reporting_state: None,
                    sensitivity: Some(vec![6]),
                    threshold_high: Some(vec![7, 8]),
                    threshold_low: Some(vec![10, 11, 12]),
                }),
            })
            .build();
        let test = async move {
            let result = facade
                .set_feature_report(
                    InputDeviceMatchArgs::default(),
                    FeatureReport {
                        sensor: Some(SensorFeatureReport {
                            report_interval: None,
                            reporting_state: None,
                            sensitivity: Some(vec![6]),
                            threshold_high: Some(vec![7, 8]),
                            threshold_low: Some(vec![10, 11, 12]),
                        }),
                    },
                )
                .await;
            assert!(result.is_ok());
        };
        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn parse_json_feature_report() {
        let mut map = Map::new();

        let mut sensor_map = Map::new();
        sensor_map.insert("report_interval".to_string(), Value::Number(Number::from(100)));
        sensor_map.insert("reporting_state".to_string(), Value::Number(Number::from(3)));
        sensor_map.insert(
            "sensitivity".to_string(),
            Value::Array(vec![
                Value::Number(Number::from(50)),
                Value::Number(Number::from(150)),
                Value::Number(Number::from(200)),
            ]),
        );
        sensor_map.insert(
            "threshold_high".to_string(),
            Value::Array(vec![
                Value::Number(Number::from(1)),
                Value::Number(Number::from(2)),
                Value::Number(Number::from(3)),
                Value::Number(Number::from(4)),
            ]),
        );

        map.insert("sensor".to_string(), Value::Object(sensor_map));

        let result = SerializableFeatureReport::deserialize(Value::Object(map));
        assert!(result.is_ok());
        assert_eq!(
            result.unwrap(),
            SerializableFeatureReport {
                sensor: Some(SerializableSensorFeatureReport {
                    report_interval: Some(100),
                    reporting_state: Some(3),
                    sensitivity: Some(vec![50, 150, 200]),
                    threshold_high: Some(vec![1, 2, 3, 4]),
                    threshold_low: None,
                })
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn parse_json_feature_report_invalid_array_entry() {
        let mut map = Map::new();

        let mut sensor_map = Map::new();
        sensor_map.insert("report_interval".to_string(), Value::Number(Number::from(100)));
        sensor_map.insert(
            "sensitivity".to_string(),
            Value::Array(vec![
                Value::Number(Number::from(50)),
                Value::Number(Number::from(150)),
                Value::Number(Number::from(200)),
            ]),
        );
        sensor_map.insert(
            "threshold_high".to_string(),
            Value::Array(vec![
                Value::Number(Number::from(1)),
                Value::Number(Number::from(2)),
                Value::String("invalid".to_string()),
                Value::Number(Number::from(4)),
            ]),
        );

        map.insert("sensor".to_string(), Value::Object(sensor_map));

        let result = SerializableFeatureReport::deserialize(Value::Object(map));
        assert!(!result.is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn parse_json_feature_report_invalid_array() {
        let mut map = Map::new();

        let mut sensor_map = Map::new();
        sensor_map.insert("report_interval".to_string(), Value::Number(Number::from(100)));
        sensor_map.insert(
            "sensitivity".to_string(),
            Value::Array(vec![
                Value::Number(Number::from(50)),
                Value::Number(Number::from(150)),
                Value::Number(Number::from(200)),
            ]),
        );
        sensor_map.insert("threshold_high".to_string(), Value::Number(Number::from(1234)));

        map.insert("sensor".to_string(), Value::Object(sensor_map));

        let result = SerializableFeatureReport::deserialize(Value::Object(map));
        assert!(!result.is_ok());
    }
}

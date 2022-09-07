// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

mod subcommands;

use {
    anyhow::{Context, Result},
    args::{PrintInputReportCommand, PrintInputReportSubcommand},
    fidl_fuchsia_io as fio,
    futures::lock::Mutex,
    std::{io::Write, sync::Arc},
};

pub async fn print_input_report(
    cmd: &PrintInputReportCommand,
    writer: Arc<Mutex<impl Write + Send + Sync + 'static>>,
    dev: fio::DirectoryProxy,
) -> Result<()> {
    match cmd.subcommand {
        PrintInputReportSubcommand::Descriptor(ref subcmd) => {
            subcommands::descriptor::descriptor(subcmd, writer, dev)
                .await
                .context("Descriptor subcommand failed")?;
        }
        PrintInputReportSubcommand::Feature(ref subcmd) => {
            subcommands::feature::feature(subcmd, writer, dev)
                .await
                .context("Feature subcommand failed")?;
        }
        PrintInputReportSubcommand::Get(ref subcmd) => {
            subcommands::get::get(subcmd, writer, dev).await.context("Get subcommand failed")?;
        }
        PrintInputReportSubcommand::Read(ref subcmd) => {
            subcommands::read::read(subcmd, writer, dev).await.context("Read subcommand failed")?;
        }
    };
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context, Result},
        argh::FromArgs,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_input as finput, fidl_fuchsia_input_report as fir, fidl_fuchsia_io as fio,
        fuchsia_async::{self as fasync, Task},
        fuchsia_component::server::{FidlService, ServiceFs},
        futures::{lock::Mutex, Future, FutureExt, StreamExt, TryStreamExt},
        std::sync::Arc,
    };

    enum InputDeviceRequestStream {
        InputDeviceA(fir::InputDeviceRequestStream),
    }

    /// Creates a mock input device at `/dev/class/input-device/A`, invokes
    /// `driver` with `cmd`, and invokes `on_input_device_a_request` whenever
    /// the mock input device receives an input device request. The output of
    /// `print_input_report` that is normally written to its `writer` parameter
    /// is returned.
    async fn test_print_input_report<AFut: Future<Output = Result<()>> + Send + 'static>(
        cmd: PrintInputReportCommand,
        on_input_device_a_request: impl Fn(fir::InputDeviceRequest) -> AFut,
    ) -> Result<String> {
        // Create a virtual file system that can serve input devices.
        let mut service_fs = ServiceFs::new_local();
        let mut dir = service_fs.dir("class");
        let mut dir = dir.dir("input-device");
        dir.add_service_at("A", FidlService::from(InputDeviceRequestStream::InputDeviceA));

        // Create a directory proxy to access the input devices.
        let (dev, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .context("Failed to create FIDL proxy")?;
        service_fs
            .serve_connection(server_end.into_channel())
            .context("Failed to serve connection")?;

        // Run the command and mock input device servers.
        let writer = Arc::new(Mutex::new(Vec::new()));
        futures::select! {
            _ = service_fs.for_each_concurrent(None, |request: InputDeviceRequestStream| async {
                match request {
                    InputDeviceRequestStream::InputDeviceA(stream) => if let Err(e) = stream
                            .map(|result| result.context("Failed to get input device request for input device A"))
                            .try_for_each(|request| on_input_device_a_request(request))
                            .await
                        {
                            panic!("Failed to handle input device requests for input device A: {}", e);
                        }
                }
            }) => {
                anyhow::bail!("Prematurely completed serving input device requests");
            },
            res = print_input_report(&cmd, Arc::clone(&writer), dev).fuse() => res.unwrap(),
        }

        let buffer = (*writer.lock().await).clone();
        String::from_utf8(buffer).context("Failed to convert print_input_report output to a string")
    }

    /// Responds to `reader`'s requests for input reports by sending
    /// `input_reports`.
    fn run_input_report_reader_server(
        input_reports: Vec<fir::InputReport>,
        reader: ServerEnd<fir::InputReportsReaderMarker>,
    ) -> Result<()> {
        let reader = reader.into_stream().context("Failed to convert reader into a stream")?;
        Task::spawn(async move {
            if let Err(e) = reader
                .map(|result| result.context("Failed input report reader request"))
                .try_for_each(|request| {
                    let mut input_reports = Ok(input_reports.clone());
                    async move {
                        match request {
                            fir::InputReportsReaderRequest::ReadInputReports { responder } => {
                                responder
                                    .send(&mut input_reports)
                                    .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                    .context("Failed to respond to ReadInputReports request")?;
                            }
                        }
                        Ok(())
                    }
                })
                .await
            {
                panic!("Failed to process input report reader requests: {}", e);
            }
        })
        .detach();
        Ok(())
    }

    // print-input-report tests
    #[fasync::run_singlethreaded(test)]
    async fn test_get_feature() -> Result<()> {
        let cmd = PrintInputReportCommand::from_args(
            &["print-input-report"],
            &["feature", "class/input-device/A"],
        )
        .unwrap();
        let output = test_print_input_report(
            cmd,
            |request: fir::InputDeviceRequest| async move {
                match request {
                    fir::InputDeviceRequest::GetFeatureReport { responder } => {
                        let mut feature_report = Ok(fir::FeatureReport {
                            sensor: Some(fir::SensorFeatureReport {
                                report_interval: Some(294),
                                sensitivity: Some(vec![8539, 912, 2]),
                                reporting_state: Some(fir::SensorReportingState::ReportThresholdEvents),
                                threshold_high: Some(vec![91, 144, 1]),
                                threshold_low: Some(vec![3, 8, 240]),
                                sampling_rate: Some(1111),
                                ..fir::SensorFeatureReport::EMPTY
                            }),
                            touch: Some(fir::TouchFeatureReport {
                                input_mode: Some(fir::TouchConfigurationInputMode::WindowsPrecisionTouchpadCollection),
                                selective_reporting: Some(fir::SelectiveReportingFeatureReport {
                                    surface_switch: Some(false),
                                    button_switch: Some(true),
                                    ..fir::SelectiveReportingFeatureReport::EMPTY
                                }),
                                ..fir::TouchFeatureReport::EMPTY
                            }),
                            ..fir::FeatureReport::EMPTY
                        });
                        responder
                            .send(&mut feature_report)
                            .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                            .context("Failed to respond to GetFeatureReport request")?;
                    }
                    _ => {}
                }
                Ok(())
            },
        )
        .await?;

        assert_eq!(
            output,
            r#"Feature from file: "class/input-device/A"
{
   sensor: {
      report_interval: 294
      sensitivity: [8539, 912, 2]
      reporting_state: ReportThresholdEvents
      threshold_high: [91, 144, 1]
      threshold_low: [3, 8, 240]
      sampling_rate: 1111
   }
   touch: {
      input_mode: WindowsPrecisionTouchpadCollection
      selective_reporting: {
         surface_switch: false
         button_switch: true
      }
   }
}
"#
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_descriptor() -> Result<()> {
        let cmd = PrintInputReportCommand::from_args(
            &["print-input-report"],
            &["descriptor", "class/input-device/A"],
        )
        .unwrap();
        let output = test_print_input_report(cmd, |request: fir::InputDeviceRequest| async move {
            match request {
                fir::InputDeviceRequest::GetDescriptor { responder } => {
                    let descriptor = fir::DeviceDescriptor {
                        device_info: Some(fir::DeviceInfo {
                            vendor_id: 123,
                            product_id: 987,
                            version: 555,
                        }),
                        mouse: Some(fir::MouseDescriptor {
                            input: Some(fir::MouseInputDescriptor {
                                movement_x: Some(fir::Axis {
                                    range: fir::Range { min: -23, max: 48 },
                                    unit: fir::Unit { type_: fir::UnitType::Degrees, exponent: 3 },
                                }),
                                movement_y: Some(fir::Axis {
                                    range: fir::Range { min: -2, max: 848 },
                                    unit: fir::Unit {
                                        type_: fir::UnitType::EnglishAngularVelocity,
                                        exponent: 10,
                                    },
                                }),
                                scroll_v: Some(fir::Axis {
                                    range: fir::Range { min: 412, max: 3333 },
                                    unit: fir::Unit {
                                        type_: fir::UnitType::SiLinearAcceleration,
                                        exponent: 23,
                                    },
                                }),
                                scroll_h: Some(fir::Axis {
                                    range: fir::Range { min: -2000, max: -10 },
                                    unit: fir::Unit { type_: fir::UnitType::Meters, exponent: 2 },
                                }),
                                buttons: Some(vec![2, 244, 99]),
                                position_x: Some(fir::Axis {
                                    range: fir::Range { min: 0, max: 0 },
                                    unit: fir::Unit { type_: fir::UnitType::Webers, exponent: 0 },
                                }),
                                position_y: Some(fir::Axis {
                                    range: fir::Range { min: -1, max: 1 },
                                    unit: fir::Unit {
                                        type_: fir::UnitType::Candelas,
                                        exponent: 999,
                                    },
                                }),
                                ..fir::MouseInputDescriptor::EMPTY
                            }),
                            ..fir::MouseDescriptor::EMPTY
                        }),
                        sensor: Some(fir::SensorDescriptor {
                            input: Some(vec![
                                fir::SensorInputDescriptor {
                                    values: Some(vec![
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 123, max: 456 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::Grams,
                                                    exponent: -12,
                                                },
                                            },
                                            type_: fir::SensorType::AccelerometerY,
                                        },
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 32, max: 76 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::Pascals,
                                                    exponent: 2929,
                                                },
                                            },
                                            type_: fir::SensorType::GyroscopeY,
                                        },
                                    ]),
                                    report_id: Some(2),
                                    ..fir::SensorInputDescriptor::EMPTY
                                },
                                fir::SensorInputDescriptor { ..fir::SensorInputDescriptor::EMPTY },
                            ]),
                            feature: Some(vec![
                                fir::SensorFeatureDescriptor {
                                    report_interval: Some(fir::Axis {
                                        range: fir::Range { min: -123, max: 789 },
                                        unit: fir::Unit {
                                            type_: fir::UnitType::Lux,
                                            exponent: 999,
                                        },
                                    }),
                                    sensitivity: Some(vec![
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 1111, max: 2222 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::Seconds,
                                                    exponent: 567,
                                                },
                                            },
                                            type_: fir::SensorType::LightIlluminance,
                                        },
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 100, max: 1000 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::None,
                                                    exponent: -2462,
                                                },
                                            },
                                            type_: fir::SensorType::LightBlue,
                                        },
                                    ]),
                                    supports_reporting_state: Some(true),
                                    threshold_high: Some(vec![
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 321, max: 654 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::Other,
                                                    exponent: 4,
                                                },
                                            },
                                            type_: fir::SensorType::MagnetometerZ,
                                        },
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 89, max: 98 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::SiLinearAcceleration,
                                                    exponent: -765,
                                                },
                                            },
                                            type_: fir::SensorType::LightRed,
                                        },
                                    ]),
                                    threshold_low: Some(vec![
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 121, max: 212 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::Degrees,
                                                    exponent: 10101,
                                                },
                                            },
                                            type_: fir::SensorType::MagnetometerX,
                                        },
                                        fir::SensorAxis {
                                            axis: fir::Axis {
                                                range: fir::Range { min: 1001, max: 9889 },
                                                unit: fir::Unit {
                                                    type_: fir::UnitType::Webers,
                                                    exponent: -10101,
                                                },
                                            },
                                            type_: fir::SensorType::AccelerometerY,
                                        },
                                    ]),
                                    sampling_rate: Some(fir::Axis {
                                        range: fir::Range { min: 19283, max: 91827 },
                                        unit: fir::Unit {
                                            type_: fir::UnitType::Meters,
                                            exponent: 92929,
                                        },
                                    }),
                                    report_id: Some(255),
                                    ..fir::SensorFeatureDescriptor::EMPTY
                                },
                                fir::SensorFeatureDescriptor {
                                    ..fir::SensorFeatureDescriptor::EMPTY
                                },
                            ]),
                            ..fir::SensorDescriptor::EMPTY
                        }),
                        touch: Some(fir::TouchDescriptor {
                            input: Some(fir::TouchInputDescriptor {
                                contacts: Some(vec![
                                    fir::ContactInputDescriptor {
                                        position_x: Some(fir::Axis {
                                            range: fir::Range { min: 1, max: 2 },
                                            unit: fir::Unit {
                                                type_: fir::UnitType::None,
                                                exponent: 0,
                                            },
                                        }),
                                        position_y: Some(fir::Axis {
                                            range: fir::Range { min: 2, max: 3 },
                                            unit: fir::Unit {
                                                type_: fir::UnitType::Other,
                                                exponent: 100000,
                                            },
                                        }),
                                        pressure: Some(fir::Axis {
                                            range: fir::Range { min: 3, max: 4 },
                                            unit: fir::Unit {
                                                type_: fir::UnitType::Grams,
                                                exponent: -991,
                                            },
                                        }),
                                        contact_width: Some(fir::Axis {
                                            range: fir::Range { min: 5, max: 6 },
                                            unit: fir::Unit {
                                                type_: fir::UnitType::EnglishAngularVelocity,
                                                exponent: 123,
                                            },
                                        }),
                                        contact_height: Some(fir::Axis {
                                            range: fir::Range { min: 7, max: 8 },
                                            unit: fir::Unit {
                                                type_: fir::UnitType::Pascals,
                                                exponent: 100,
                                            },
                                        }),
                                        ..fir::ContactInputDescriptor::EMPTY
                                    },
                                    fir::ContactInputDescriptor {
                                        ..fir::ContactInputDescriptor::EMPTY
                                    },
                                ]),
                                max_contacts: Some(444444),
                                touch_type: Some(fir::TouchType::Touchscreen),
                                buttons: Some(vec![1, 2, 3]),
                                ..fir::TouchInputDescriptor::EMPTY
                            }),
                            feature: Some(fir::TouchFeatureDescriptor {
                                supports_input_mode: Some(false),
                                supports_selective_reporting: Some(true),
                                ..fir::TouchFeatureDescriptor::EMPTY
                            }),
                            ..fir::TouchDescriptor::EMPTY
                        }),
                        keyboard: Some(fir::KeyboardDescriptor {
                            input: Some(fir::KeyboardInputDescriptor {
                                keys3: Some(vec![finput::Key::L, finput::Key::CapsLock]),
                                ..fir::KeyboardInputDescriptor::EMPTY
                            }),
                            output: Some(fir::KeyboardOutputDescriptor {
                                leds: Some(vec![fir::LedType::NumLock, fir::LedType::Kana]),
                                ..fir::KeyboardOutputDescriptor::EMPTY
                            }),
                            ..fir::KeyboardDescriptor::EMPTY
                        }),
                        consumer_control: Some(fir::ConsumerControlDescriptor {
                            input: Some(fir::ConsumerControlInputDescriptor {
                                buttons: Some(vec![
                                    fir::ConsumerControlButton::VolumeUp,
                                    fir::ConsumerControlButton::Reboot,
                                ]),
                                ..fir::ConsumerControlInputDescriptor::EMPTY
                            }),
                            ..fir::ConsumerControlDescriptor::EMPTY
                        }),
                        ..fir::DeviceDescriptor::EMPTY
                    };
                    responder
                        .send(descriptor)
                        .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                        .context("Failed to respond to GetDescriptor request")?;
                }
                _ => {}
            }
            Ok(())
        })
        .await?;

        assert_eq!(
            output,
            r#"Descriptor from file: "class/input-device/A"
{
   device_info: {
      vendor_id: 123
      product_id: 987
      version: 555
   }
   mouse: {
      input: {
         movement_x: {
            range: {
               min: -23
               max: 48
            }
            unit: {
               type: Degrees
               exponent: 3
            }
         }
         movement_y: {
            range: {
               min: -2
               max: 848
            }
            unit: {
               type: EnglishAngularVelocity
               exponent: 10
            }
         }
         scroll_v: {
            range: {
               min: 412
               max: 3333
            }
            unit: {
               type: SiLinearAcceleration
               exponent: 23
            }
         }
         scroll_h: {
            range: {
               min: -2000
               max: -10
            }
            unit: {
               type: Meters
               exponent: 2
            }
         }
         buttons: [2, 244, 99]
         position_x: {
            range: {
               min: 0
               max: 0
            }
            unit: {
               type: Webers
               exponent: 0
            }
         }
         position_y: {
            range: {
               min: -1
               max: 1
            }
            unit: {
               type: Candelas
               exponent: 999
            }
         }
      }
   }
   sensor: {
      input: [
         {
            report_id: 2
            values: [
               {
                  axis: {
                     range: {
                        min: 123
                        max: 456
                     }
                     unit: {
                        type: Grams
                        exponent: -12
                     }
                  }
                  type: AccelerometerY
               },
               {
                  axis: {
                     range: {
                        min: 32
                        max: 76
                     }
                     unit: {
                        type: Pascals
                        exponent: 2929
                     }
                  }
                  type: GyroscopeY
               }
            ]
         },
         {
            report_id: None
            values: None
         }
      ]
      feature: [
         {
            report_interval: {
               range: {
                  min: -123
                  max: 789
               }
               unit: {
                  type: Lux
                  exponent: 999
               }
            }
            sensitivity: [
               {
                  axis: {
                     range: {
                        min: 1111
                        max: 2222
                     }
                     unit: {
                        type: Seconds
                        exponent: 567
                     }
                  }
                  type: LightIlluminance
               },
               {
                  axis: {
                     range: {
                        min: 100
                        max: 1000
                     }
                     unit: {
                        type: None
                        exponent: -2462
                     }
                  }
                  type: LightBlue
               }
            ]
            supports_reporting_state: true
            threshold_high: [
               {
                  axis: {
                     range: {
                        min: 321
                        max: 654
                     }
                     unit: {
                        type: Other
                        exponent: 4
                     }
                  }
                  type: MagnetometerZ
               },
               {
                  axis: {
                     range: {
                        min: 89
                        max: 98
                     }
                     unit: {
                        type: SiLinearAcceleration
                        exponent: -765
                     }
                  }
                  type: LightRed
               }
            ]
            threshold_low: [
               {
                  axis: {
                     range: {
                        min: 121
                        max: 212
                     }
                     unit: {
                        type: Degrees
                        exponent: 10101
                     }
                  }
                  type: MagnetometerX
               },
               {
                  axis: {
                     range: {
                        min: 1001
                        max: 9889
                     }
                     unit: {
                        type: Webers
                        exponent: -10101
                     }
                  }
                  type: AccelerometerY
               }
            ]
            sampling_rate: {
               range: {
                  min: 19283
                  max: 91827
               }
               unit: {
                  type: Meters
                  exponent: 92929
               }
            }
            report_id: 255
         },
         {
            report_interval: None
            sensitivity: None
            supports_reporting_state: None
            threshold_high: None
            threshold_low: None
            sampling_rate: None
            report_id: None
         }
      ]
   }
   touch: {
      input: {
         contacts: [
            {
               position_x: {
                  range: {
                     min: 1
                     max: 2
                  }
                  unit: {
                     type: None
                     exponent: 0
                  }
               }
               position_y: {
                  range: {
                     min: 2
                     max: 3
                  }
                  unit: {
                     type: Other
                     exponent: 100000
                  }
               }
               pressure: {
                  range: {
                     min: 3
                     max: 4
                  }
                  unit: {
                     type: Grams
                     exponent: -991
                  }
               }
               contact_width: {
                  range: {
                     min: 5
                     max: 6
                  }
                  unit: {
                     type: EnglishAngularVelocity
                     exponent: 123
                  }
               }
               contact_height: {
                  range: {
                     min: 7
                     max: 8
                  }
                  unit: {
                     type: Pascals
                     exponent: 100
                  }
               }
            },
            {
               position_x: None
               position_y: None
               pressure: None
               contact_width: None
               contact_height: None
            }
         ]
         max_contacts: 444444
         touch_type: Touchscreen
         buttons: [1, 2, 3]
      }
      feature: {
         supports_input_mode: false
         supports_selective_reporting: true
      }
   }
   keyboard: {
      input: {
         keys3: [L, CapsLock]
      }
      output: {
         leds: [NumLock, Kana]
      }
   }
   consumer_control: {
      input: {
         buttons: [VolumeUp, Reboot]
      }
   }
}
"#
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_input_report() -> Result<()> {
        let cmd = PrintInputReportCommand::from_args(
            &["print-input-report"],
            &["get", "class/input-device/A", "mouse"],
        )
        .unwrap();
        let output = test_print_input_report(cmd, |request: fir::InputDeviceRequest| async move {
            match request {
                fir::InputDeviceRequest::GetInputReport { device_type, responder } => {
                    match device_type {
                        fir::DeviceType::Mouse => {
                            let mut input_report = Ok(fir::InputReport {
                                event_time: Some(123),
                                mouse: Some(fir::MouseInputReport {
                                    pressed_buttons: Some(vec![]),
                                    ..fir::MouseInputReport::EMPTY
                                }),
                                sensor: Some(fir::SensorInputReport {
                                    ..fir::SensorInputReport::EMPTY
                                }),
                                touch: Some(fir::TouchInputReport {
                                    ..fir::TouchInputReport::EMPTY
                                }),
                                keyboard: Some(fir::KeyboardInputReport {
                                    ..fir::KeyboardInputReport::EMPTY
                                }),
                                consumer_control: Some(fir::ConsumerControlInputReport {
                                    ..fir::ConsumerControlInputReport::EMPTY
                                }),
                                ..fir::InputReport::EMPTY
                            });
                            responder
                                .send(&mut input_report)
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to GetInputReport request")?;
                        }
                        _ => {}
                    }
                }
                _ => {}
            }
            Ok(())
        })
        .await?;

        assert_eq!(
            output,
            r#"Reading a report from "class/input-device/A":
Report from file: "class/input-device/A"
{
   event_time: 123
   trace_id: None
   report_id: None
   mouse: {
      movement_x: None
      movement_y: None
      scroll_v: None
      scroll_h: None
      pressed_buttons: []
      position_x: None
      position_y: None
   }
   sensor: {
      values: None
   }
   touch: {
      contacts: None
      pressed_buttons: None
   }
   keyboard: {
      pressed_keys3: None
   }
   consumer_control: {
      pressed_buttons: None
   }
}
"#
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_two_input_reports() -> Result<()> {
        let cmd = PrintInputReportCommand::from_args(
            &["print-input-report"],
            &["read", "devpath", "class/input-device/A", "--num-reads", "2"],
        )
        .unwrap();
        let output = test_print_input_report(cmd, |request: fir::InputDeviceRequest| async move {
            match request {
                fir::InputDeviceRequest::GetInputReportsReader { reader, .. } => {
                    run_input_report_reader_server(
                        vec![
                            fir::InputReport {
                                event_time: Some(123),
                                mouse: Some(fir::MouseInputReport {
                                    movement_x: Some(-92341),
                                    movement_y: Some(27),
                                    scroll_v: Some(-2930),
                                    scroll_h: Some(12383),
                                    pressed_buttons: Some(vec![5, 28]),
                                    position_x: Some(-9999),
                                    position_y: Some(1111),
                                    ..fir::MouseInputReport::EMPTY
                                }),
                                touch: Some(fir::TouchInputReport {
                                    contacts: Some(vec![
                                        fir::ContactInputReport {
                                            contact_id: Some(93002),
                                            position_x: Some(12),
                                            position_y: Some(-2117),
                                            pressure: Some(2),
                                            contact_width: Some(888),
                                            contact_height: Some(166),
                                            ..fir::ContactInputReport::EMPTY
                                        },
                                        fir::ContactInputReport {
                                            contact_id: Some(452),
                                            position_x: Some(4),
                                            position_y: Some(-1233),
                                            pressure: Some(200),
                                            contact_width: Some(24),
                                            contact_height: Some(3333),
                                            ..fir::ContactInputReport::EMPTY
                                        },
                                    ]),
                                    pressed_buttons: Some(vec![8, 24]),
                                    ..fir::TouchInputReport::EMPTY
                                }),
                                trace_id: Some(523234),
                                report_id: Some(108),
                                ..fir::InputReport::EMPTY
                            },
                            fir::InputReport {
                                event_time: Some(456),
                                sensor: Some(fir::SensorInputReport {
                                    values: Some(vec![-149, 921399, 0]),
                                    ..fir::SensorInputReport::EMPTY
                                }),
                                keyboard: Some(fir::KeyboardInputReport {
                                    pressed_keys3: Some(vec![
                                        finput::Key::A,
                                        finput::Key::Semicolon,
                                        finput::Key::KeypadPlus,
                                    ]),
                                    ..fir::KeyboardInputReport::EMPTY
                                }),
                                consumer_control: Some(fir::ConsumerControlInputReport {
                                    pressed_buttons: Some(vec![
                                        fir::ConsumerControlButton::VolumeUp,
                                        fir::ConsumerControlButton::Reboot,
                                    ]),
                                    ..fir::ConsumerControlInputReport::EMPTY
                                }),
                                trace_id: Some(9327),
                                report_id: Some(2),
                                ..fir::InputReport::EMPTY
                            },
                        ],
                        reader,
                    )
                    .context("Failed to run input report reader server")?;
                }
                _ => {}
            }
            Ok(())
        })
        .await?;

        assert_eq!(
            output,
            r#"Reading reports from "class/input-device/A"
Report from file "class/input-device/A"
{
   event_time: 123
   trace_id: 523234
   report_id: 108
   mouse: {
      movement_x: -92341
      movement_y: 27
      scroll_v: -2930
      scroll_h: 12383
      pressed_buttons: [5, 28]
      position_x: -9999
      position_y: 1111
   }
   sensor: None
   touch: {
      contacts: [
         {
            contact_id: 93002
            position_x: 12
            position_y: -2117
            pressure: 2
            contact_width: 888
            contact_height: 166
         },
         {
            contact_id: 452
            position_x: 4
            position_y: -1233
            pressure: 200
            contact_width: 24
            contact_height: 3333
         }
      ]
      pressed_buttons: [8, 24]
   }
   keyboard: None
   consumer_control: None
}
Report from file "class/input-device/A"
{
   event_time: 456
   trace_id: 9327
   report_id: 2
   mouse: None
   sensor: {
      values: [-149, 921399, 0]
   }
   touch: None
   keyboard: {
      pressed_keys3: [A, Semicolon, KeypadPlus]
   }
   consumer_control: {
      pressed_buttons: [VolumeUp, Reboot]
   }
}
"#
        );
        Ok(())
    }
}

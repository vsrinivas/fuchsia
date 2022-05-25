// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
pub mod descriptor;
pub mod feature;
pub mod get;
pub mod read;

use {
    anyhow::{Context, Result},
    fidl::endpoints::Proxy,
    fidl_fuchsia_input_report as fir, fidl_fuchsia_io as fio,
    futures::stream::{Stream, TryStreamExt},
    std::{
        fmt::{Debug, Display},
        io::Write,
        path::{Path, PathBuf},
    },
};

fn connect_to_input_device(
    dev: &fio::DirectoryProxy,
    device_path: impl AsRef<Path> + Debug,
) -> Result<fir::InputDeviceProxy> {
    let (proxy, server) = fidl::endpoints::create_proxy::<fio::NodeMarker>()?;
    let device_path = device_path
        .as_ref()
        .as_os_str()
        .to_str()
        .ok_or(anyhow::anyhow!("Failed to get device path string"))?;
    dev.open(
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        device_path,
        server,
    )
    .context("Failed to open device file")?;
    Ok(fir::InputDeviceProxy::new(proxy.into_channel().unwrap()))
}

async fn get_all_input_device_paths(
    dev: &fio::DirectoryProxy,
) -> Result<impl Stream<Item = Result<PathBuf>>> {
    const INPUT_DEVICE_DIR: &str = "class/input-report";
    let input_device_dir = io_util::open_directory(
        &dev,
        &Path::new(INPUT_DEVICE_DIR),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .with_context(|| format!("Failed to open {}", INPUT_DEVICE_DIR))?;
    let input_device_paths = device_watcher::watch_for_files(&input_device_dir)
        .await
        .context("Failed to watch for input device /dev files")?;
    Ok(input_device_paths
        .map_ok(|input_device_path| Path::new(INPUT_DEVICE_DIR).join(input_device_path)))
}

fn write_display(
    writer: &mut impl Write,
    name: &str,
    indent: usize,
    x: &impl Display,
) -> Result<()> {
    writeln!(writer, "{:spaces$}{}: {}", "", name, x, spaces = indent * 3)?;
    Ok(())
}

fn write_display_option(
    writer: &mut impl Write,
    name: &str,
    indent: usize,
    option: &Option<impl Display>,
) -> Result<()> {
    if let Some(x) = option {
        write_display(writer, name, indent, x)?;
    } else {
        writeln!(writer, "{:spaces$}{}: None", "", name, spaces = indent * 3)?;
    }
    Ok(())
}

fn write_debug(writer: &mut impl Write, name: &str, indent: usize, x: &impl Debug) -> Result<()> {
    writeln!(writer, "{:spaces$}{}: {:?}", "", name, x, spaces = indent * 3)?;
    Ok(())
}

fn write_debug_option(
    writer: &mut impl Write,
    name: &str,
    indent: usize,
    option: &Option<impl Debug>,
) -> Result<()> {
    if let Some(x) = option {
        write_debug(writer, name, indent, x)?;
    } else {
        writeln!(writer, "{:spaces$}{}: None", "", name, spaces = indent * 3)?;
    }
    Ok(())
}

fn write_struct<T, W: Write>(
    writer: &mut W,
    name: &str,
    indent: usize,
    x: &T,
    write_some: impl Fn(&mut W, &T, usize) -> Result<()>,
) -> Result<()> {
    let spaces = indent * 3;
    writeln!(writer, "{:spaces$}{}: {{", "", name)?;
    write_some(writer, x, indent + 1)?;
    writeln!(writer, "{:spaces$}}}", "")?;
    Ok(())
}

fn write_struct_option<T, W: Write>(
    writer: &mut W,
    name: &str,
    indent: usize,
    option: &Option<T>,
    write_some: impl Fn(&mut W, &T, usize) -> Result<()>,
) -> Result<()> {
    if let Some(x) = option {
        write_struct(writer, name, indent, x, write_some)?;
    } else {
        writeln!(writer, "{:spaces$}{}: None", "", name, spaces = indent * 3)?;
    }
    Ok(())
}

fn write_display_vec_option(
    writer: &mut impl Write,
    name: &str,
    indent: usize,
    option: &Option<Vec<impl Display>>,
) -> Result<()> {
    write!(writer, "{:spaces$}{}: ", "", name, spaces = indent * 3)?;
    if let Some(elems) = option {
        write!(writer, "[")?;
        for (i, elem) in elems.iter().enumerate() {
            write!(writer, "{}", elem)?;
            if i < elems.len() - 1 {
                write!(writer, ", ")?;
            }
        }
        writeln!(writer, "]")?;
    } else {
        writeln!(writer, "None")?;
    }
    Ok(())
}

fn write_struct_vec_option<T, W: Write>(
    writer: &mut W,
    name: &str,
    indent: usize,
    option: &Option<Vec<T>>,
    write_some: impl Fn(&mut W, &T, usize) -> Result<()>,
) -> Result<()> {
    let spaces1 = indent * 3;
    let spaces2 = (indent + 1) * 3;
    write!(writer, "{:spaces1$}{}: ", "", name)?;
    if let Some(elems) = option {
        if elems.is_empty() {
            writeln!(writer, "[]")?;
        } else {
            write!(writer, "[")?;
            for (i, elem) in elems.iter().enumerate() {
                writeln!(writer, "\n{:spaces2$}{{", "")?;
                write_some(writer, elem, indent + 2)?;
                write!(writer, "{:spaces2$}}}", "")?;
                if i < elems.len() - 1 {
                    write!(writer, ",")?;
                }
            }
            writeln!(writer, "\n{:spaces1$}]", "")?;
        }
    } else {
        writeln!(writer, "None")?;
    }
    Ok(())
}

fn write_debug_vec_option(
    writer: &mut impl Write,
    name: &str,
    indent: usize,
    option: &Option<Vec<impl Debug>>,
) -> Result<()> {
    let spaces = indent * 3;
    write!(writer, "{:spaces$}{}: ", "", name)?;
    if let Some(elems) = option {
        write!(writer, "[")?;
        for (i, elem) in elems.iter().enumerate() {
            write!(writer, "{:?}", elem)?;
            if i < elems.len() - 1 {
                write!(writer, ", ")?;
            }
        }
        writeln!(writer, "]")?;
    } else {
        writeln!(writer, "None")?;
    }
    Ok(())
}

pub fn write_input_report(writer: &mut impl Write, input_report: &fir::InputReport) -> Result<()> {
    writeln!(writer, "{{")?;
    write_display_option(writer, "event_time", 1, &input_report.event_time)?;
    write_display_option(writer, "trace_id", 1, &input_report.trace_id)?;
    write_display_option(writer, "report_id", 1, &input_report.report_id)?;
    write_struct_option(writer, "mouse", 1, &input_report.mouse, |writer, mouse, indent| {
        write_display_option(writer, "movement_x", indent, &mouse.movement_x)?;
        write_display_option(writer, "movement_y", indent, &mouse.movement_y)?;
        write_display_option(writer, "scroll_v", indent, &mouse.scroll_v)?;
        write_display_option(writer, "scroll_h", indent, &mouse.scroll_h)?;
        write_display_vec_option(writer, "pressed_buttons", indent, &mouse.pressed_buttons)?;
        write_display_option(writer, "position_x", indent, &mouse.position_x)?;
        write_display_option(writer, "position_y", indent, &mouse.position_y)?;
        Ok(())
    })?;
    write_struct_option(writer, "sensor", 1, &input_report.sensor, |writer, sensor, indent| {
        write_display_vec_option(writer, "values", indent, &sensor.values)?;
        Ok(())
    })?;
    write_struct_option(writer, "touch", 1, &input_report.touch, |writer, touch, indent| {
        write_struct_vec_option(
            writer,
            "contacts",
            indent,
            &touch.contacts,
            |writer, contact, indent| {
                write_display_option(writer, "contact_id", indent, &contact.contact_id)?;
                write_display_option(writer, "position_x", indent, &contact.position_x)?;
                write_display_option(writer, "position_y", indent, &contact.position_y)?;
                write_display_option(writer, "pressure", indent, &contact.pressure)?;
                write_display_option(writer, "contact_width", indent, &contact.contact_width)?;
                write_display_option(writer, "contact_height", indent, &contact.contact_height)?;
                Ok(())
            },
        )?;
        write_display_vec_option(writer, "pressed_buttons", indent, &touch.pressed_buttons)?;
        Ok(())
    })?;
    write_struct_option(
        writer,
        "keyboard",
        1,
        &input_report.keyboard,
        |writer, keyboard, indent| {
            write_debug_vec_option(writer, "pressed_keys3", indent, &keyboard.pressed_keys3)?;
            Ok(())
        },
    )?;
    write_struct_option(
        writer,
        "consumer_control",
        1,
        &input_report.consumer_control,
        |writer, consumer_control, indent| {
            write_debug_vec_option(
                writer,
                "pressed_buttons",
                indent,
                &consumer_control.pressed_buttons,
            )?;
            Ok(())
        },
    )?;
    writeln!(writer, "}}")?;
    Ok(())
}

fn write_feature_report(
    writer: &mut impl Write,
    feature_report: &fir::FeatureReport,
) -> Result<()> {
    writeln!(writer, "{{")?;
    write_struct_option(writer, "sensor", 1, &feature_report.sensor, |writer, sensor, indent| {
        write_display_option(writer, "report_interval", indent, &sensor.report_interval)?;
        write_display_vec_option(writer, "sensitivity", indent, &sensor.sensitivity)?;
        write_debug_option(writer, "reporting_state", indent, &sensor.reporting_state)?;
        write_display_vec_option(writer, "threshold_high", indent, &sensor.threshold_high)?;
        write_display_vec_option(writer, "threshold_low", indent, &sensor.threshold_low)?;
        write_display_option(writer, "sampling_rate", indent, &sensor.sampling_rate)?;
        Ok(())
    })?;
    write_struct_option(writer, "touch", 1, &feature_report.touch, |writer, touch, indent| {
        write_debug_option(writer, "input_mode", indent, &touch.input_mode)?;
        write_struct_option(
            writer,
            "selective_reporting",
            indent,
            &touch.selective_reporting,
            |writer, selective_reportng, indent| {
                write_display_option(
                    writer,
                    "surface_switch",
                    indent,
                    &selective_reportng.surface_switch,
                )?;
                write_display_option(
                    writer,
                    "button_switch",
                    indent,
                    &selective_reportng.button_switch,
                )?;
                Ok(())
            },
        )?;
        Ok(())
    })?;
    writeln!(writer, "}}")?;
    Ok(())
}

fn write_descriptor(writer: &mut impl Write, descriptor: &fir::DeviceDescriptor) -> Result<()> {
    fn write_axis(
        writer: &mut impl Write,
        name: &str,
        indent: usize,
        axis: &fir::Axis,
    ) -> Result<()> {
        write_struct(writer, name, indent, axis, |writer, axis, indent| {
            write_struct(writer, "range", indent, &axis.range, |writer, range, indent| {
                write_display(writer, "min", indent, &range.min)?;
                write_display(writer, "max", indent, &range.max)?;
                Ok(())
            })?;
            write_struct(writer, "unit", indent, &axis.unit, |writer, unit, indent| {
                write_debug(writer, "type", indent, &unit.type_)?;
                write_display(writer, "exponent", indent, &unit.exponent)?;
                Ok(())
            })?;
            Ok(())
        })?;
        Ok(())
    }
    fn write_axis_option(
        writer: &mut impl Write,
        name: &str,
        indent: usize,
        axis: &Option<fir::Axis>,
    ) -> Result<()> {
        if let Some(axis) = axis {
            write_axis(writer, name, indent, axis)?;
        } else {
            writeln!(writer, "{:spaces$}{}: None", "", name, spaces = indent * 3)?;
        }
        Ok(())
    }
    fn write_sensor_axis_vec_option(
        writer: &mut impl Write,
        name: &str,
        indent: usize,
        sensor_axes: &Option<Vec<fir::SensorAxis>>,
    ) -> Result<()> {
        write_struct_vec_option(
            writer,
            name,
            indent,
            sensor_axes,
            |writer, sensor_axis, indent| {
                write_axis(writer, "axis", indent, &sensor_axis.axis)?;
                write_debug(writer, "type", indent, &sensor_axis.type_)?;
                Ok(())
            },
        )?;
        Ok(())
    }
    writeln!(writer, "{{")?;
    write_struct_option(
        writer,
        "device_info",
        1,
        &descriptor.device_info,
        |writer, device_info, indent| {
            write_display(writer, "vendor_id", indent, &device_info.vendor_id)?;
            write_display(writer, "product_id", indent, &device_info.product_id)?;
            write_display(writer, "version", indent, &device_info.version)?;
            Ok(())
        },
    )?;
    write_struct_option(writer, "mouse", 1, &descriptor.mouse, |writer, mouse, indent| {
        write_struct_option(writer, "input", indent, &mouse.input, |writer, input, indent| {
            write_axis_option(writer, "movement_x", indent, &input.movement_x)?;
            write_axis_option(writer, "movement_y", indent, &input.movement_y)?;
            write_axis_option(writer, "scroll_v", indent, &input.scroll_v)?;
            write_axis_option(writer, "scroll_h", indent, &input.scroll_h)?;
            write_display_vec_option(writer, "buttons", indent, &input.buttons)?;
            write_axis_option(writer, "position_x", indent, &input.position_x)?;
            write_axis_option(writer, "position_y", indent, &input.position_y)?;
            Ok(())
        })?;
        Ok(())
    })?;
    write_struct_option(writer, "sensor", 1, &descriptor.sensor, |writer, sensor, indent| {
        write_struct_vec_option(
            writer,
            "input",
            indent,
            &sensor.input,
            |writer, input, indent| {
                write_display_option(writer, "report_id", indent, &input.report_id)?;
                write_sensor_axis_vec_option(writer, "values", indent, &input.values)?;
                Ok(())
            },
        )?;
        write_struct_vec_option(
            writer,
            "feature",
            indent,
            &sensor.feature,
            |writer, feature, indent| {
                write_axis_option(writer, "report_interval", indent, &feature.report_interval)?;
                write_sensor_axis_vec_option(writer, "sensitivity", indent, &feature.sensitivity)?;
                write_display_option(
                    writer,
                    "supports_reporting_state",
                    indent,
                    &feature.supports_reporting_state,
                )?;
                write_sensor_axis_vec_option(
                    writer,
                    "threshold_high",
                    indent,
                    &feature.threshold_high,
                )?;
                write_sensor_axis_vec_option(
                    writer,
                    "threshold_low",
                    indent,
                    &feature.threshold_low,
                )?;
                write_axis_option(writer, "sampling_rate", indent, &feature.sampling_rate)?;
                write_display_option(writer, "report_id", indent, &feature.report_id)?;
                Ok(())
            },
        )?;
        Ok(())
    })?;
    write_struct_option(writer, "touch", 1, &descriptor.touch, |writer, touch, indent| {
        write_struct_option(writer, "input", indent, &touch.input, |writer, input, indent| {
            write_struct_vec_option(
                writer,
                "contacts",
                indent,
                &input.contacts,
                |writer, contact, indent| {
                    write_axis_option(writer, "position_x", indent, &contact.position_x)?;
                    write_axis_option(writer, "position_y", indent, &contact.position_y)?;
                    write_axis_option(writer, "pressure", indent, &contact.pressure)?;
                    write_axis_option(writer, "contact_width", indent, &contact.contact_width)?;
                    write_axis_option(writer, "contact_height", indent, &contact.contact_height)?;
                    Ok(())
                },
            )?;
            write_display_option(writer, "max_contacts", indent, &input.max_contacts)?;
            write_debug_option(writer, "touch_type", indent, &input.touch_type)?;
            write_display_vec_option(writer, "buttons", indent, &input.buttons)?;
            Ok(())
        })?;
        write_struct_option(
            writer,
            "feature",
            indent,
            &touch.feature,
            |writer, feature, indent| {
                write_display_option(
                    writer,
                    "supports_input_mode",
                    indent,
                    &feature.supports_input_mode,
                )?;
                write_display_option(
                    writer,
                    "supports_selective_reporting",
                    indent,
                    &feature.supports_selective_reporting,
                )?;
                Ok(())
            },
        )?;
        Ok(())
    })?;
    write_struct_option(
        writer,
        "keyboard",
        1,
        &descriptor.keyboard,
        |writer, keyboard, indent| {
            write_struct_option(
                writer,
                "input",
                indent,
                &keyboard.input,
                |writer, input, indent| {
                    write_debug_vec_option(writer, "keys3", indent, &input.keys3)?;
                    Ok(())
                },
            )?;
            write_struct_option(
                writer,
                "output",
                indent,
                &keyboard.output,
                |writer, output, indent| {
                    write_debug_vec_option(writer, "leds", indent, &output.leds)?;
                    Ok(())
                },
            )?;
            Ok(())
        },
    )?;
    write_struct_option(
        writer,
        "consumer_control",
        1,
        &descriptor.consumer_control,
        |writer, consumer_control, indent| {
            write_struct_option(
                writer,
                "input",
                indent,
                &consumer_control.input,
                |writer, input, indent| {
                    write_debug_vec_option(writer, "buttons", indent, &input.buttons)?;
                    Ok(())
                },
            )?;
            Ok(())
        },
    )?;
    writeln!(writer, "}}")?;
    Ok(())
}

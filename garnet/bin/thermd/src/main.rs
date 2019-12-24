// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fdio;
use fidl_fuchsia_hardware_gpu_clock as fidl_gpu;
use fidl_fuchsia_hardware_thermal as fidl_thermal;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use std::convert::TryInto;
use std::fs::File;
use std::os::unix::io::AsRawFd;
use std::path::Path;

static CPU_THERMAL_DRIVER_PATH: &str = "/dev/class/thermal/000";
static GPU_CLOCK_DRIVER_PATH: &str = "/dev/class/gpu-thermal/000";

fn match_driver(path: &Path) -> Result<File, Error> {
    let dir = path.parent().ok_or(format_err!("Invalid driver path"))?;
    let name = path.file_name().ok_or(format_err!("Invalid driver name"))?;

    let dir_fd = File::open(&dir).context("Failed to open driver path")?;
    let st = fdio::watch_directory(&dir_fd, zx::sys::ZX_TIME_INFINITE, |event, matched| {
        if event == fdio::WatchEvent::AddFile && matched == name {
            Err(zx::Status::STOP)
        } else {
            Ok(())
        }
    });

    if st == zx::Status::STOP {
        Ok(File::open(&path).context("Failed to open file")?)
    } else {
        Err(format_err!("Watcher terminated without finding sensor"))
    }
}

fn get_service_handle(fd: &File) -> Result<u32, Error> {
    let fd = fd.as_raw_fd();
    let mut handle = 0;
    zx::Status::ok(unsafe { fdio::fdio_sys::fdio_get_service_handle(fd, &mut handle) })?;
    Ok(handle)
}

fn get_service_channel(path: &Path) -> Result<zx::Channel, Error> {
    let fd = match_driver(path).context("Failed to match driver")?;
    let handle = get_service_handle(&fd).context("Failed to get service handle")?;
    Ok(zx::Channel::from(unsafe { zx::Handle::from_raw(handle) }))
}

fn get_cpu_thermal_proxy() -> Result<fidl_thermal::DeviceProxy, Error> {
    let channel = get_service_channel(Path::new(CPU_THERMAL_DRIVER_PATH))?;
    Ok(fidl_thermal::DeviceProxy::new(fasync::Channel::from_channel(channel)?))
}

fn get_gpu_clock_proxy() -> Result<fidl_gpu::ClockProxy, Error> {
    let channel = get_service_channel(Path::new(GPU_CLOCK_DRIVER_PATH))?;
    Ok(fidl_gpu::ClockProxy::new(fasync::Channel::from_channel(channel)?))
}

async fn get_thermal_info(
    proxy: &fidl_thermal::DeviceProxy,
) -> Result<fidl_thermal::ThermalDeviceInfo, Error> {
    let (status, info) = proxy.get_device_info().await?;
    zx::Status::ok(status)
        .context(format!("get_device_info failed for CPU thermal device: {}", status))?;
    let info = *info.ok_or(format_err!("get_device_info returned mising info"))?;

    if info.num_trip_points == 0 {
        Err(format_err!("Trip points not supported"))
    } else if !info.active_cooling && !info.passive_cooling {
        Err(format_err!("No active or passive cooling present on device"))
    } else {
        Ok(info)
    }
}

async fn get_notify_port(proxy: &fidl_thermal::DeviceProxy) -> Result<zx::Port, Error> {
    let (status, port) = proxy.get_state_change_port().await?;
    zx::Status::ok(status)
        .context(format!("get_state_change_port failed for CPU thermal device: {}", status))?;
    let port = port.ok_or(format_err!("get_state_change_port returned missing info"))?;
    Ok(port)
}

fn get_trip_idx(packet: &zx::Packet, info: &fidl_thermal::ThermalDeviceInfo) -> Result<u32, Error> {
    let trip_idx: u32 = packet.key().try_into()?;
    if trip_idx < info.num_trip_points {
        Ok(trip_idx)
    } else {
        Err(format_err!("Invalid trip index: terminating thermd"))
    }
}

async fn handle_packet(
    packet: zx::Packet,
    info: &fidl_thermal::ThermalDeviceInfo,
    cpu_proxy: &fidl_thermal::DeviceProxy,
    gpu_proxy: &fidl_gpu::ClockProxy,
) -> Result<(), Error> {
    let trip_idx = get_trip_idx(&packet, info)? as usize;

    fx_log_info!("Trip point updated ({})", trip_idx);

    if info.passive_cooling {
        let big_cluster_opp = info.trip_point_info[trip_idx].big_cluster_dvfs_opp;

        // Set DVFS Opp for Big Cluster.
        let status = cpu_proxy
            .set_dvfs_operating_point(
                big_cluster_opp,
                fidl_thermal::PowerDomain::BigClusterPowerDomain,
            )
            .await?;
        if zx::Status::ok(status).is_err() {
            fx_log_err!("ERROR: Failed to set DVFS OPP for big cluster");
        }

        // Check if it's big little.
        if info.big_little {
            // Set the DVFS Opp for Little Cluster.
            let little_cluster_opp = info.trip_point_info[trip_idx].little_cluster_dvfs_opp;
            let status = cpu_proxy
                .set_dvfs_operating_point(
                    little_cluster_opp,
                    fidl_thermal::PowerDomain::LittleClusterPowerDomain,
                )
                .await?;
            if zx::Status::ok(status).is_err() {
                fx_log_err!("ERROR: Failed to set DVFS OPP for little cluster");
            }
        }
    }

    if info.active_cooling {
        let fan_level = info.trip_point_info[trip_idx].fan_level;
        let status = cpu_proxy.set_fan_level(fan_level).await?;
        if zx::Status::ok(status).is_err() {
            fx_log_err!("ERROR: Failed to set fan level");
        }
    }

    if info.gpu_throttling {
        let gpu_clk_freq_source = info.trip_point_info[trip_idx].gpu_clk_freq_source;
        let status = gpu_proxy.set_frequency_source(gpu_clk_freq_source).await?;
        if zx::Status::ok(status).is_err() {
            fx_log_err!("ERROR: Failed to change GPU clock freq source");
        }
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["thermd"])?;

    fx_log_info!("started");

    // TODO(braval): This sleep is not needed here but leaving it here
    // since the Intel thermd has it. Clean up when both deamons are
    // unified
    zx::Duration::from_seconds(3).sleep();

    let cpu_thermal_proxy = get_cpu_thermal_proxy()?;
    let gpu_clock_proxy = get_gpu_clock_proxy()?;
    let thermal_info = get_thermal_info(&cpu_thermal_proxy).await?;
    let port = get_notify_port(&cpu_thermal_proxy).await?;

    fx_log_info!("Entering loop...");

    loop {
        let packet = port.wait(zx::Time::INFINITE).context("Failed to wait on port")?;
        handle_packet(packet, &thermal_info, &cpu_thermal_proxy, &gpu_clock_proxy).await?;
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        futures::TryStreamExt,
        std::sync::{Arc, RwLock},
    };

    fn generate_packet(key: u64) -> zx::Packet {
        zx::Packet::from_user_packet(key, 0, zx::UserPacket::from_u8_array([0; 32]))
    }

    fn spawn_fake_thermal_service(
        state: Arc<RwLock<DeviceState>>,
        mut stream: fidl_thermal::DeviceRequestStream,
    ) {
        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fidl_thermal::DeviceRequest::GetDeviceInfo { responder }) => {
                        let _st = responder.send(0, None);
                    }
                    Some(fidl_thermal::DeviceRequest::SetDvfsOperatingPoint {
                        op_idx,
                        power_domain,
                        responder,
                    }) => {
                        match power_domain {
                            fidl_thermal::PowerDomain::BigClusterPowerDomain => {
                                state.write().unwrap().big_op_idx = op_idx;
                            }
                            fidl_thermal::PowerDomain::LittleClusterPowerDomain => {
                                state.write().unwrap().little_op_idx = op_idx;
                            }
                        }
                        let _st = responder.send(0);
                    }
                    Some(fidl_thermal::DeviceRequest::SetFanLevel { fan_level, responder }) => {
                        state.write().unwrap().fan_level = fan_level;
                        let _st = responder.send(0);
                    }
                    _ => {
                        println!("Unknown request");
                    }
                }
            }
        })
    }

    fn spawn_fake_gpu_service(
        state: Arc<RwLock<DeviceState>>,
        mut stream: fidl_gpu::ClockRequestStream,
    ) {
        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fidl_gpu::ClockRequest::SetFrequencySource { source, responder }) => {
                        state.write().unwrap().gpu_freq_source = source;
                        let _st = responder.send(0);
                    }
                    _ => {
                        println!("Unknown request");
                    }
                }
            }
        })
    }

    // test that invalid trip point key is rejected
    async fn test_trip_point_key(mut info: fidl_thermal::ThermalDeviceInfo) {
        info.num_trip_points = 2;
        assert_eq!(get_trip_idx(&generate_packet(0), &info).unwrap(), 0);
        assert_eq!(get_trip_idx(&generate_packet(1), &info).unwrap(), 1);
        assert!(get_trip_idx(&generate_packet(2), &info).is_err());
    }

    // tests that handle_packet will call the expected APIs to configure the thermal service
    // based on packet input
    async fn test_iterate_trip_points(
        info: fidl_thermal::ThermalDeviceInfo,
        cpu_proxy: &fidl_thermal::DeviceProxy,
        gpu_proxy: &fidl_gpu::ClockProxy,
        state: &Arc<RwLock<DeviceState>>,
    ) {
        println!("test_iterate_trip_points");

        for i in 0..info.num_trip_points {
            println!("Sending trip point {}", i);
            let packet = generate_packet(i.into());
            let status = handle_packet(packet, &info, &cpu_proxy, &gpu_proxy).await;
            let trip_info = info.trip_point_info[i as usize];

            assert!(status.is_ok());
            assert_eq!(state.read().unwrap().big_op_idx, trip_info.big_cluster_dvfs_opp);
            assert_eq!(state.read().unwrap().little_op_idx, trip_info.little_cluster_dvfs_opp);
            assert_eq!(state.read().unwrap().fan_level, trip_info.fan_level);
            assert_eq!(state.read().unwrap().gpu_freq_source, trip_info.gpu_clk_freq_source);
        }
    }

    fn generate_thermal_info() -> fidl_thermal::ThermalDeviceInfo {
        let empty_trip_point = fidl_thermal::ThermalTemperatureInfo {
            up_temp_celsius: 0.0,
            down_temp_celsius: 0.0,
            fan_level: 0,
            big_cluster_dvfs_opp: 0,
            little_cluster_dvfs_opp: 0,
            gpu_clk_freq_source: 0,
        };

        let empty_opp_entry = fidl_thermal::OperatingPointEntry { freq_hz: 0, volt_uv: 0 };
        let empty_opp =
            fidl_thermal::OperatingPoint { opp: [empty_opp_entry; 16], latency: 0, count: 0 };

        let mut info = fidl_thermal::ThermalDeviceInfo {
            active_cooling: true,
            passive_cooling: true,
            gpu_throttling: true,
            num_trip_points: 3,
            big_little: true,
            critical_temp_celsius: 0.0,
            trip_point_info: [empty_trip_point; 16],
            opps: [empty_opp; 2],
        };

        info.trip_point_info[0] = fidl_thermal::ThermalTemperatureInfo {
            up_temp_celsius: 0.0,
            down_temp_celsius: 0.0,
            fan_level: 1000,
            big_cluster_dvfs_opp: 1001,
            little_cluster_dvfs_opp: 1002,
            gpu_clk_freq_source: 1003,
        };

        info.trip_point_info[1] = fidl_thermal::ThermalTemperatureInfo {
            up_temp_celsius: 0.0,
            down_temp_celsius: 0.0,
            fan_level: 2000,
            big_cluster_dvfs_opp: 2001,
            little_cluster_dvfs_opp: 2002,
            gpu_clk_freq_source: 2003,
        };

        info.trip_point_info[2] = fidl_thermal::ThermalTemperatureInfo {
            up_temp_celsius: 0.0,
            down_temp_celsius: 0.0,
            fan_level: 3000,
            big_cluster_dvfs_opp: 3001,
            little_cluster_dvfs_opp: 3002,
            gpu_clk_freq_source: 3003,
        };

        info
    }

    fn thermal_test_setup() -> (
        fidl_thermal::ThermalDeviceInfo,
        fidl_thermal::DeviceProxy,
        fidl_thermal::DeviceRequestStream,
    ) {
        let (client, server) = zx::Channel::create().unwrap();
        let client = fasync::Channel::from_channel(client).unwrap();
        let server = fasync::Channel::from_channel(server).unwrap();
        let proxy = fidl_thermal::DeviceProxy::new(client);
        let stream = fidl_thermal::DeviceRequestStream::from_channel(server);
        (generate_thermal_info(), proxy, stream)
    }

    fn gpu_test_setup() -> (fidl_gpu::ClockProxy, fidl_gpu::ClockRequestStream) {
        let (client, server) = zx::Channel::create().unwrap();
        let client = fasync::Channel::from_channel(client).unwrap();
        let server = fasync::Channel::from_channel(server).unwrap();
        let proxy = fidl_gpu::ClockProxy::new(client);
        let stream = fidl_gpu::ClockRequestStream::from_channel(server);
        (proxy, stream)
    }

    struct DeviceState {
        big_op_idx: u16,
        little_op_idx: u16,
        fan_level: u32,
        gpu_freq_source: u32,
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_packet() {
        let (t_info, t_proxy, t_stream) = thermal_test_setup();
        let (g_proxy, g_stream) = gpu_test_setup();

        let state: Arc<RwLock<DeviceState>> = Arc::new(RwLock::new(DeviceState {
            big_op_idx: 0,
            little_op_idx: 0,
            fan_level: 0,
            gpu_freq_source: 0,
        }));

        spawn_fake_thermal_service(state.clone(), t_stream);
        spawn_fake_gpu_service(state.clone(), g_stream);

        test_trip_point_key(t_info).await;
        test_iterate_trip_points(t_info, &t_proxy, &g_proxy, &state).await;
    }
}

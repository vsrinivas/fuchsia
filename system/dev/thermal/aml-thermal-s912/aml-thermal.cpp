// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"
#include <ddk/device.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <soc/aml-common/aml-thermal.h>
#include <string.h>
#include <zircon/device/thermal.h>
#include <zircon/syscalls/port.h>

namespace thermal {
namespace {

// Worker-thread's internal loop deadline in seconds.
constexpr int kDeadline = 5;

} // namespace

zx_status_t AmlThermal::Create(zx_device_t* device) {
    zxlogf(INFO, "aml_thermal: driver begin...\n");
    zx_status_t status;

    pdev_protocol_t pdev_proto;
    status = device_get_protocol(device, ZX_PROTOCOL_PDEV, &pdev_proto);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not get platform device protocol: %d\n", status);
        return status;
    }
    ddk::PDevProtocolProxy pdev(&pdev_proto);

    size_t actual;
    gpio_protocol_t fan0_gpio_proto;
    status = pdev.GetProtocol(ZX_PROTOCOL_GPIO, FAN_CTL0, &fan0_gpio_proto, sizeof(fan0_gpio_proto),
                              &actual);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not get fan0 gpio protocol: %d\n", status);
        return status;
    }

    gpio_protocol_t fan1_gpio_proto;
    status = pdev.GetProtocol(ZX_PROTOCOL_GPIO, FAN_CTL1, &fan1_gpio_proto, sizeof(fan1_gpio_proto),
                              &actual);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not get fan1 gpio protocol: %d\n", status);
        return status;
    }

    scpi_protocol_t scpi_proto;
    status = pdev.GetProtocol(ZX_PROTOCOL_SCPI, 0, &scpi_proto, sizeof(scpi_proto), &actual);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not get scpi protocol: %d\n", status);
        return status;
    }

    ddk::ScpiProtocolProxy scpi(&scpi_proto);
    uint32_t sensor_id;
    status = scpi.GetSensor("aml_thermal", &sensor_id);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not thermal get sensor: %d\n", status);
        return status;
    }

    zx::port port;
    status = zx::port::create(0, &port);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not configure port: %d\n", status);
        return status;
    }

    auto thermal = fbl::make_unique<AmlThermal>(device, pdev_proto, fan0_gpio_proto,
                                                fan1_gpio_proto, scpi_proto, sensor_id, port);

    status = thermal->DdkAdd("vim-thermal", DEVICE_ADD_INVISIBLE);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not add driver: %d\n", status);
        return status;
    }

    // Perform post-construction initialization before device is made visible.
    status = thermal->Init();
    if (status != ZX_OK) {
        THERMAL_ERROR("could not initialize thermal driver: %d\n", status);
        thermal->DdkRemove();
        return status;
    }

    thermal->DdkMakeVisible();

    // devmgr is now in charge of this device.
    __UNUSED auto _ = thermal.release();
    return ZX_OK;
}

zx_status_t AmlThermal::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                                 size_t out_len, size_t* actual) {
    switch (op) {
    // Input: None, Output: thermal_device_info_t.
    case IOCTL_THERMAL_GET_DEVICE_INFO: {
        if (out_len != sizeof(thermal_device_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(static_cast<thermal_device_info_t*>(out_buf), &info_,
               sizeof(thermal_device_info_t));
        *actual = sizeof(thermal_device_info_t);
        return ZX_OK;
    }

    // Input: None, Output: zx_handle_t.
    case IOCTL_THERMAL_GET_STATE_CHANGE_PORT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx::port dup;
        port_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
        *static_cast<zx_handle_t*>(out_buf) = dup.release();
        *actual = sizeof(zx_handle_t);
        return ZX_OK;
    }

    // Input: uint32_t, Output: None.
    case IOCTL_THERMAL_SET_FAN_LEVEL: {
        if (in_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto status = SetFanLevel(*static_cast<const FanLevel*>(in_buf));
        if (status != ZX_OK) {
            return status;
        }
        return ZX_OK;
    }

    // Input: None, Output: uint32_t.
    case IOCTL_THERMAL_GET_FAN_LEVEL: {
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        *static_cast<uint32_t*>(out_buf) = fan_level_;
        *actual = sizeof(uint32_t);
        return ZX_OK;
    }

    // Input: uint32_t, Output: scpi_opp_t.
    case IOCTL_THERMAL_GET_DVFS_INFO: {
        if (in_len != sizeof(uint32_t) || out_len != sizeof(scpi_opp_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto in = *static_cast<const uint8_t*>(in_buf);
        if (in > MAX_DVFS_DOMAINS) {
            return ZX_ERR_INVALID_ARGS;
        }
        scpi_opp_t opps;
        auto status = scpi_.GetDvfsInfo(in, &opps);
        if (status != ZX_OK) {
            return status;
        }
        memcpy(static_cast<scpi_opp_t*>(out_buf), &opps,
               sizeof(scpi_opp_t));
        *actual = sizeof(scpi_opp_t);
        return ZX_OK;
    }

    // Input: uint32_t, Output: uint32_t.
    case IOCTL_THERMAL_GET_DVFS_OPP: {
        if (in_len != sizeof(uint32_t) || out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto in = *static_cast<const uint8_t*>(in_buf);
        if (in == BIG_CLUSTER_POWER_DOMAIN) {
            *static_cast<uint32_t*>(out_buf) = cur_bigcluster_opp_idx_;
        } else if (in == LITTLE_CLUSTER_POWER_DOMAIN) {
            *static_cast<uint32_t*>(out_buf) = cur_littlecluster_opp_idx_;
        } else {
            return ZX_ERR_INVALID_ARGS;
        }
        *actual = sizeof(uint32_t);
        return ZX_OK;
    }

    // Input: dvfs_info_t, Output: None.
    case IOCTL_THERMAL_SET_DVFS_OPP: {
        if (in_len != sizeof(dvfs_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto in = static_cast<const dvfs_info_t*>(in_buf);
        bool set_new_opp = false;
        if (in->power_domain == BIG_CLUSTER_POWER_DOMAIN) {
            if (cur_bigcluster_opp_idx_ != in->op_idx) {
                set_new_opp = true;
                cur_bigcluster_opp_idx_ = in->op_idx;
            }
        } else {
            if (cur_littlecluster_opp_idx_ != in->op_idx) {
                set_new_opp = true;
                cur_littlecluster_opp_idx_ = in->op_idx;
            }
        }

        if (set_new_opp) {
            return scpi_.SetDvfsIdx(static_cast<uint8_t>(in->power_domain), in->op_idx);
        } else {
            return ZX_OK;
        }
    }

    // Input: None, Output: uint32_t.
    case IOCTL_THERMAL_GET_TEMPERATURE: {
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        *static_cast<uint32_t*>(out_buf) = temperature_;
        *actual = sizeof(uint32_t);
        return ZX_OK;
    }

    default: {
        return ZX_ERR_NOT_SUPPORTED;
    }
    }
    return ZX_OK;
}

void AmlThermal::DdkRelease() {
    const auto status = thrd_join(worker_, nullptr);
    if (status != ZX_OK) {
        THERMAL_ERROR("worker thread failed: %d\n", status);
    }
}

void AmlThermal::DdkUnbind() {
    sync_completion_signal(&quit_);
}

zx_status_t AmlThermal::Init() {
    auto status = fan0_gpio_.ConfigOut(0);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not configure FAN_CTL0 gpio: %d\n", status);
        return status;
    }

    status = fan1_gpio_.ConfigOut(0);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not configure FAN_CTL1 gpio: %d\n", status);
        return status;
    }

    size_t read;
    status = DdkGetMetadata(DEVICE_METADATA_PRIVATE, &info_, sizeof(thermal_device_info_t), &read);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not read device metadata: %d\n", status);
        return status;
    } else if (read != sizeof(thermal_device_info_t)) {
        THERMAL_ERROR("could not read device metadata\n");
        return ZX_ERR_NO_MEMORY;
    }

    status = scpi_.GetDvfsInfo(BIG_CLUSTER_POWER_DOMAIN, &info_.opps[0]);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not get bigcluster dvfs opps: %d\n", status);
        return status;
    }

    status = scpi_.GetDvfsInfo(LITTLE_CLUSTER_POWER_DOMAIN, &info_.opps[1]);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not get littlecluster dvfs opps: %d\n", status);
        return status;
    }

    auto start_thread = [](void* arg) { return static_cast<AmlThermal*>(arg)->Worker(); };
    status = thrd_create_with_name(&worker_, start_thread, this, "aml_thermal_notify_thread");
    if (status != ZX_OK) {
        THERMAL_ERROR("could not start worker thread: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t AmlThermal::NotifyThermalDaemon(uint32_t trip_index) const {
    zx_port_packet_t pkt;
    pkt.key = trip_index;
    pkt.type = ZX_PKT_TYPE_USER;
    return port_.queue(&pkt);
}

zx_status_t AmlThermal::SetFanLevel(FanLevel level) {
    // Levels per individual system fan.
    uint8_t fan0_level;
    uint8_t fan1_level;

    switch (level) {
    case FAN_L0:
        fan0_level = 0;
        fan1_level = 0;
        break;
    case FAN_L1:
        fan0_level = 1;
        fan1_level = 0;
        break;
    case FAN_L2:
        fan0_level = 0;
        fan1_level = 1;
        break;
    case FAN_L3:
        fan0_level = 1;
        fan1_level = 1;
        break;
    default:
        THERMAL_ERROR("unknown fan level: %d\n", level);
        return ZX_ERR_INVALID_ARGS;
    }

    auto status = fan0_gpio_.Write(fan0_level);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not set FAN_CTL0 level: %d\n", status);
        return status;
    }

    status = fan1_gpio_.Write(fan1_level);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not set FAN_CTL1 level: %d\n", status);
        return status;
    }

    fan_level_ = level;
    return ZX_OK;
}

int AmlThermal::Worker() {
    zx_status_t status;
    uint32_t trip_pt = 0;
    const uint32_t trip_limit = info_.num_trip_points - 1;
    bool crit = false;
    bool signal = false;

    // Notify thermal daemon of initial settings.
    status = NotifyThermalDaemon(trip_pt);
    if (status != ZX_OK) {
        THERMAL_ERROR("could not notify thermal daemon: %d\n", status);
        return status;
    }

    do {
        status = scpi_.GetSensorValue(sensor_id_, &temperature_);
        if (status != ZX_OK) {
            THERMAL_ERROR("could not read temperature: %d\n", status);
            return status;
        }

        signal = true;
        if (trip_pt != trip_limit && temperature_ >= info_.trip_point_info[trip_pt + 1].up_temp) {
            trip_pt++; // Triggered next trip point.
        } else if (trip_pt && temperature_ < info_.trip_point_info[trip_pt].down_temp) {
            if (trip_pt == trip_limit) {
                // A prev trip point triggered, so the temperature is falling
                // down below the critical temperature.  Make a note of that.
                crit = false;
            }
            trip_pt--; // Triggered prev trip point.
        } else if (trip_pt == trip_limit && temperature_ >= info_.critical_temp && !crit) {
            // The device temperature is crossing the critical temperature, set
            // the CPU freq to the lowest possible setting to ensure the
            // temperature doesn't rise any further.
            crit = true;
            status = scpi_.SetDvfsIdx(BIG_CLUSTER_POWER_DOMAIN, 0);
            if (status != ZX_OK) {
                THERMAL_ERROR("unable to set DVFS OPP for Big cluster\n");
                return status;
            }

            status = scpi_.SetDvfsIdx(LITTLE_CLUSTER_POWER_DOMAIN, 0);
            if (status != ZX_OK) {
                THERMAL_ERROR("unable to set DVFS OPP for Little cluster\n");
                return status;
            }
        } else {
            signal = false;
        }

        if (signal) {
            // Notify the thermal daemon about which trip point triggered.
            status = NotifyThermalDaemon(trip_pt);
            if (status != ZX_OK) {
                THERMAL_ERROR("could not notify thermal daemon: %d\n", status);
                return status;
            }
        }

    } while (sync_completion_wait(&quit_, ZX_SEC(kDeadline)) == ZX_ERR_TIMED_OUT);
    return ZX_OK;
}

} // namespace thermal

extern "C" zx_status_t aml_thermal_bind(void* ctx, zx_device_t* device) {
    return thermal::AmlThermal::Create(device);
}

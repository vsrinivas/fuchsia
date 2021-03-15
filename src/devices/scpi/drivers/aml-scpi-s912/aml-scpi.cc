// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-scpi.h"

#include <fuchsia/hardware/mailbox/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "src/devices/scpi/drivers/aml-scpi-s912/aml-scpi-s912-bind.h"

namespace scpi {

zx_status_t AmlSCPI::GetMailbox(uint32_t cmd, mailbox_type_t* mailbox) {
  if (!mailbox || !VALID_CMD(cmd)) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (uint32_t i = 0; i < countof(aml_low_priority_cmds); i++) {
    if (cmd == aml_low_priority_cmds[i]) {
      *mailbox = MAILBOX_TYPE_AP_NS_LOW_PRIORITY_MAILBOX;
      return ZX_OK;
    }
  }

  for (uint32_t i = 0; i < countof(aml_high_priority_cmds); i++) {
    if (cmd == aml_high_priority_cmds[i]) {
      *mailbox = MAILBOX_TYPE_AP_NS_HIGH_PRIORITY_MAILBOX;
      return ZX_OK;
    }
  }

  for (uint32_t i = 0; i < countof(aml_secure_cmds); i++) {
    if (cmd == aml_secure_cmds[i]) {
      *mailbox = MAILBOX_TYPE_AP_SECURE_MAILBOX;
      return ZX_OK;
    }
  }
  *mailbox = MAILBOX_TYPE_INVALID_MAILBOX;
  return ZX_ERR_NOT_FOUND;
}

zx_status_t AmlSCPI::ExecuteCommand(void* rx_buf, size_t rx_size, void* tx_buf, size_t tx_size,
                                    uint32_t cmd, uint32_t client_id) {
  uint32_t mailbox_status = 0;
  mailbox_data_buf_t mdata;
  mdata.cmd = PACK_SCPI_CMD(cmd, client_id, 0);
  mdata.tx_buffer = tx_buf;
  mdata.tx_size = tx_size;

  mailbox_channel_t channel;
  zx_status_t status = GetMailbox(cmd, &channel.mailbox);
  if (status != ZX_OK) {
    SCPI_ERROR("aml_scpi_get_mailbox failed - error status %d", status);
    return status;
  }

  channel.rx_buffer = rx_buf;
  channel.rx_size = rx_size;

  status = mailbox_.SendCommand(&channel, &mdata);
  if (rx_buf) {
    mailbox_status = *(uint32_t*)(rx_buf);
  }
  if (status != ZX_OK || mailbox_status != 0) {
    SCPI_ERROR("mailbox_send_command failed - error status %d", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t AmlSCPI::ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx) {
  struct {
    uint32_t status;
    uint8_t idx;
    uint8_t padding[3];
  } __PACKED aml_dvfs_idx_info;

  if (!idx || power_domain >= fuchsia_hardware_thermal_MAX_DVFS_DOMAINS) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ExecuteCommand(&aml_dvfs_idx_info, sizeof(aml_dvfs_idx_info), &power_domain,
                                      sizeof(power_domain), SCPI_CMD_GET_DVFS, SCPI_CL_DVFS);
  if (status != ZX_OK) {
    return status;
  }

  *idx = aml_dvfs_idx_info.idx;

  SCPI_INFO("Current Operation point %x", aml_dvfs_idx_info.idx);
  return ZX_OK;
}

zx_status_t AmlSCPI::ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx) {
  struct {
    uint8_t power_domain;
    uint16_t idx;
    uint8_t padding;
  } __PACKED aml_dvfs_idx_info;

  if (power_domain >= fuchsia_hardware_thermal_MAX_DVFS_DOMAINS) {
    return ZX_ERR_INVALID_ARGS;
  }

  aml_dvfs_idx_info.power_domain = power_domain;
  aml_dvfs_idx_info.idx = idx;

  SCPI_INFO("OPP index for cluster %d to %d", power_domain, idx);
  return ExecuteCommand(NULL, 0, &aml_dvfs_idx_info, sizeof(aml_dvfs_idx_info), SCPI_CMD_SET_DVFS,
                        SCPI_CL_DVFS);
}

zx_status_t AmlSCPI::ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* out_opps) {
  zx_status_t status;
  struct {
    uint32_t status;
    uint8_t reserved;
    uint8_t operating_points;
    uint16_t latency;
    scpi_opp_entry_t opp[fuchsia_hardware_thermal_MAX_DVFS_OPPS];
  } __PACKED aml_dvfs_info;

  if (!out_opps || power_domain >= fuchsia_hardware_thermal_MAX_DVFS_DOMAINS) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock mailbox_lock(&lock_);

  // dvfs info already populated
  if (scpi_opp[power_domain]) {
    memcpy(out_opps, scpi_opp[power_domain], sizeof(scpi_opp_t));
    return ZX_OK;
  }

  uint32_t power_domain_scpi = 0;
  status = ExecuteCommand(&aml_dvfs_info, sizeof(aml_dvfs_info), &power_domain_scpi,
                          sizeof(power_domain_scpi), SCPI_CMD_GET_DVFS_INFO, SCPI_CL_DVFS);
  if (status != ZX_OK) {
    return status;
  }

  ZX_DEBUG_ASSERT(power_domain_scpi <= UINT8_MAX);
  power_domain = static_cast<uint8_t>(power_domain_scpi);

  out_opps->count = aml_dvfs_info.operating_points;
  out_opps->latency = aml_dvfs_info.latency;

  if (out_opps->count > fuchsia_hardware_thermal_MAX_DVFS_OPPS) {
    SCPI_ERROR("Number of operating_points greater than fuchsia_hardware_thermal_MAX_DVFS_OPPS");
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  zxlogf(INFO, "Cluster %u details", power_domain);
  zxlogf(INFO, "Number of operating_points %u", aml_dvfs_info.operating_points);
  zxlogf(INFO, "latency %u uS", aml_dvfs_info.latency);

  for (uint32_t i = 0; i < out_opps->count; i++) {
    out_opps->opp[i].freq_hz = aml_dvfs_info.opp[i].freq_hz;
    out_opps->opp[i].volt_uv = aml_dvfs_info.opp[i].volt_uv;
    zxlogf(INFO, "Operating point %d - ", i);
    zxlogf(INFO, "Freq %.4f Ghz ", (out_opps->opp[i].freq_hz) / (double)1000000000);
    zxlogf(INFO, "Voltage %.4f V", (out_opps->opp[i].volt_uv) / (double)1000);
  }

  scpi_opp[power_domain] = static_cast<scpi_opp_t*>(calloc(1, sizeof(scpi_opp_t)));
  if (!scpi_opp[power_domain]) {
    status = ZX_ERR_NO_MEMORY;
    return status;
  }

  memcpy(scpi_opp[power_domain], out_opps, sizeof(scpi_opp_t));
  return ZX_OK;
}

zx_status_t AmlSCPI::ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value) {
  struct {
    uint32_t status;
    uint16_t sensor_value;
    uint16_t padding;
  } __PACKED aml_sensor_val;

  if (!sensor_value) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ExecuteCommand(&aml_sensor_val, sizeof(aml_sensor_val), &sensor_id,
                                      sizeof(sensor_id), SCPI_CMD_SENSOR_VALUE, SCPI_CL_THERMAL);
  if (status != ZX_OK) {
    return status;
  }
  *sensor_value = aml_sensor_val.sensor_value;
  return ZX_OK;
}

zx_status_t AmlSCPI::ScpiGetSensor(const char* name, uint32_t* sensor_value) {
  struct {
    uint32_t status;
    uint16_t num_sensors;
    uint16_t padding;
  } __PACKED aml_sensor_cap;

  struct {
    uint32_t status;
    uint16_t sensor;
    uint8_t sensor_class;
    uint8_t trigger;
    char sensor_name[20];
  } __PACKED aml_sensor_info;

  if (sensor_value == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  // First let's find information about all sensors
  zx_status_t status = ExecuteCommand(&aml_sensor_cap, sizeof(aml_sensor_cap), NULL, 0,
                                      SCPI_CMD_SENSOR_CAPABILITIES, SCPI_CL_THERMAL);
  if (status != ZX_OK) {
    return status;
  }

  // Loop through all the sensors
  for (uint32_t sensor_id = 0; sensor_id < aml_sensor_cap.num_sensors; sensor_id++) {
    status = ExecuteCommand(&aml_sensor_info, sizeof(aml_sensor_info), &sensor_id,
                            sizeof(sensor_id), SCPI_CMD_SENSOR_INFO, SCPI_CL_THERMAL);
    if (status != ZX_OK) {
      return status;
    }
    if (!strncmp(name, aml_sensor_info.sensor_name, sizeof(aml_sensor_info.sensor_name))) {
      *sensor_value = sensor_id;
      break;
    }
  }
  return ZX_OK;
}

void AmlSCPI::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlSCPI::DdkRelease() { delete this; }

zx_status_t AmlSCPI::Bind() {
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_SCPI},
  };

  return DdkAdd(ddk::DeviceAddArgs("aml-scpi").set_props(props));
}

zx_status_t AmlSCPI::Create(zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto scpi_device = fbl::make_unique_checked<AmlSCPI>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = ZX_ERR_INTERNAL;
  // Get ZX_PROTOCOL_MAILBOX protocol.
  if (!scpi_device->mailbox_.is_valid()) {
    zxlogf(ERROR, "dwmac: could not obtain ZX_PROTOCOL_MAILBOX protocol: %d", status);
    return status;
  }

  mtx_init(&scpi_device->lock_, mtx_plain);

  status = scpi_device->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-scpi driver failed to get added: %d", status);
    return status;
  } else {
    zxlogf(INFO, "aml-scpi driver added");
  }

  // scpi_device intentionally leaked as it is now held by DevMgr
  __UNUSED auto ptr = scpi_device.release();

  return ZX_OK;
}

zx_status_t aml_scpi_bind(void* ctx, zx_device_t* parent) { return scpi::AmlSCPI::Create(parent); }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = aml_scpi_bind;
  return ops;
}();

}  // namespace scpi

ZIRCON_DRIVER(aml_scpi, scpi::driver_ops, "zircon", "0.1");

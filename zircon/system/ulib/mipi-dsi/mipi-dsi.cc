// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mipi-dsi/mipi-dsi.h"

#include <fuchsia/hardware/dsi/llcpp/fidl.h>
#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/allocator.h>
#include <lib/fidl/llcpp/memory.h>

#include <algorithm>
#include <cstdint>

namespace mipi_dsi {

zx::status<llcpp::fuchsia::hardware::dsi::MipiDsiCmd> MipiDsi::CreateCommandFidl(
    uint32_t tlen, uint32_t rlen, bool is_dcs, fidl::Allocator* allocator) {
  // Create a command packet
  uint8_t ch_id = kMipiDsiVirtualChanId;
  uint8_t dsi_data_type = kMipiDsiDtUnknown;
  uint32_t flags = 0;
  auto builder = ::llcpp::fuchsia::hardware::dsi::MipiDsiCmd::Builder(
      allocator->make<::llcpp::fuchsia::hardware::dsi::MipiDsiCmd::Frame>());

  switch (tlen) {
    case 0:
      if (is_dcs) {
        printf("Missing DCS Command\n");
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      if (rlen > 0) {
        dsi_data_type = kMipiDsiDtGenShortRead0;
        flags = MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
      } else {
        dsi_data_type = kMipiDsiDtGenShortWrite0;
      }
      break;
    case 1:
      if (rlen > 0) {
        dsi_data_type = is_dcs ? kMipiDsiDtDcsRead0 : kMipiDsiDtGenShortRead1;
        flags = MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
      } else {
        dsi_data_type = is_dcs ? kMipiDsiDtDcsShortWrite0 : kMipiDsiDtGenShortWrite1;
      }
      break;
    case 2:
      if (rlen > 0) {
        if (is_dcs) {
          printf("Invalid DCS GEN READ Command!\n");
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
        dsi_data_type = kMipiDsiDtGenShortRead2;
        flags = MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
      } else {
        dsi_data_type = is_dcs ? kMipiDsiDtDcsShortWrite1 : kMipiDsiDtGenShortWrite2;
      }
      break;
    default:
      if (rlen > 0) {
        printf("Invalid DSI GEN READ Command!\n");
        return zx::error(ZX_ERR_INVALID_ARGS);
      } else {
        dsi_data_type = is_dcs ? kMipiDsiDtDcsLongWrite : kMipiDsiDtGenLongWrite;
      }
      break;
  }

  builder.set_virtual_channel_id(allocator->make<uint8_t>(ch_id));
  builder.set_expected_read_length(allocator->make<uint32_t>(rlen));
  builder.set_dsi_data_type(allocator->make<uint8_t>(dsi_data_type));
  builder.set_write_length(allocator->make<uint32_t>(tlen));
  builder.set_flags(allocator->make<uint32_t>(flags));
  // packet command has been created.
  return zx::ok(builder.build());
}

zx_status_t MipiDsi::CreateCommand(const uint8_t* tbuf, size_t tlen, uint8_t* rbuf, size_t rlen,
                                   bool is_dcs, mipi_dsi_cmd_t* cmd) {
  // Create a command packet
  cmd->virt_chn_id = kMipiDsiVirtualChanId;
  cmd->pld_data_list = tbuf;  // tbuf is allowed to be null
  cmd->pld_data_count = tlen;
  cmd->rsp_data_list = rbuf;  // rbuf is allowed to be null if rlen is 0
  cmd->rsp_data_count = rlen;
  cmd->flags = 0;
  cmd->dsi_data_type = kMipiDsiDtUnknown;

  switch (tlen) {
    case 0:
      if (is_dcs) {
        printf("Missing DCS Command\n");
        return ZX_ERR_INVALID_ARGS;
      }
      if (rbuf && rlen > 0) {
        cmd->dsi_data_type = kMipiDsiDtGenShortRead0;
        cmd->flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
      } else {
        cmd->dsi_data_type = kMipiDsiDtGenShortWrite0;
      }
      break;
    case 1:
      if (rbuf && rlen > 0) {
        cmd->dsi_data_type = is_dcs ? kMipiDsiDtDcsRead0 : kMipiDsiDtGenShortRead1;
        cmd->flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
      } else {
        cmd->dsi_data_type = is_dcs ? kMipiDsiDtDcsShortWrite0 : kMipiDsiDtGenShortWrite1;
      }
      break;
    case 2:
      if (rbuf && rlen > 0) {
        if (is_dcs) {
          printf("Invalid DCS GEN READ Command!\n");
          return ZX_ERR_INVALID_ARGS;
        }
        cmd->dsi_data_type = kMipiDsiDtGenShortRead2;
        cmd->flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
      } else {
        cmd->dsi_data_type = is_dcs ? kMipiDsiDtDcsShortWrite1 : kMipiDsiDtGenShortWrite2;
      }
      break;
    default:
      if (rbuf || rlen > 0) {
        printf("Invalid DSI GEN READ Command!\n");
        return ZX_ERR_INVALID_ARGS;
      } else {
        cmd->dsi_data_type = is_dcs ? kMipiDsiDtDcsLongWrite : kMipiDsiDtGenLongWrite;
      }
      break;
  }

  // packet command has been created.
  return ZX_OK;
}

}  // namespace mipi_dsi

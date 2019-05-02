// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mipi-dsi/mipi-dsi.h"

namespace mipi_dsi {

zx_status_t MipiDsi::CreateCommand(const uint8_t* tbuf, size_t tlen,
                                   uint8_t* rbuf, size_t rlen,
                                   bool is_dcs, mipi_dsi_cmd_t* cmd) {
    // Create a command packet
    cmd->virt_chn_id = MIPI_DSI_VIRTUAL_CHAN_ID;
    cmd->pld_data_list = tbuf; // tbuf is allowed to be null
    cmd->pld_data_count = tlen;
    cmd->rsp_data_list = rbuf; // rbuf is allowed to be null if rlen is 0
    cmd->rsp_data_count = rlen;
    cmd->flags = 0;
    cmd->dsi_data_type = MIPI_DSI_DT_UNKNOWN;

    switch (tlen) {
    case 0:
        if (is_dcs) {
            printf("Missing DCS Command\n");
            return ZX_ERR_INVALID_ARGS;
        }
        if (rbuf && rlen > 0) {
            cmd->dsi_data_type = MIPI_DSI_DT_GEN_SHORT_READ_0;
            cmd->flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
        } else {
            cmd->dsi_data_type = MIPI_DSI_DT_GEN_SHORT_WRITE_0;
        }
        break;
    case 1:
        if (rbuf && rlen > 0) {
            cmd->dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_READ_0 : MIPI_DSI_DT_GEN_SHORT_READ_1;
            cmd->flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
        } else {
            cmd->dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_SHORT_WRITE_0 :
                                          MIPI_DSI_DT_GEN_SHORT_WRITE_1;
        }
        break;
    case 2:
        if (rbuf && rlen > 0) {
            if (is_dcs) {
                printf("Invalid DCS GEN READ Command!\n");
                return ZX_ERR_INVALID_ARGS;
            }
            cmd->dsi_data_type = MIPI_DSI_DT_GEN_SHORT_READ_2;
            cmd->flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
        } else {
            cmd->dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_SHORT_WRITE_1 :
                                          MIPI_DSI_DT_GEN_SHORT_WRITE_2;
        }
        break;
    default:
        if (rbuf || rlen > 0) {
            printf("Invalid DSI GEN READ Command!\n");
            return ZX_ERR_INVALID_ARGS;
        } else {
            cmd->dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_LONG_WRITE : MIPI_DSI_DT_GEN_LONG_WRITE;
        }
        break;
    }

    // packet command has been created.
    return ZX_OK;
}

} //namespace mipi_dsi

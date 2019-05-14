// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>
#include "lib/mipi-dsi/mipi-dsi.h"

namespace mipi_dsi {

TEST(CreateCommmand, CommandStructure) {
    mipi_dsi_cmd_t cmd;
    uint8_t tbuf[3];
    uint8_t rbuf[3];
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                rbuf, sizeof(rbuf),
                                                false, &cmd);
    EXPECT_TRUE(status == ZX_ERR_INVALID_ARGS);
    EXPECT_TRUE(cmd.virt_chn_id == MIPI_DSI_VIRTUAL_CHAN_ID);
    EXPECT_TRUE(cmd.pld_data_list == tbuf);
    EXPECT_TRUE(cmd.pld_data_count == sizeof(tbuf));
    EXPECT_TRUE(cmd.rsp_data_list == rbuf);
    EXPECT_TRUE(cmd.rsp_data_count == sizeof(rbuf));
    EXPECT_TRUE(cmd.flags == 0);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_UNKNOWN);
}

TEST(CreateCommmand, GenShortWrite0_T1) {
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(nullptr, 0,
                                                nullptr, 0,
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_SHORT_WRITE_0);
    EXPECT_OK(status);
}

TEST(CreateCommmand, GenShortWrite0_T2) {
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(nullptr, 0,
                                                nullptr, 0,
                                                true, &cmd);
    EXPECT_TRUE(status == ZX_ERR_INVALID_ARGS);
}

TEST(CreateCommmand, GenShortWrite1_T1) {
    uint8_t tbuf[1];
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                nullptr, 0,
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_SHORT_WRITE_1);
    EXPECT_OK(status);
}

TEST(CreateCommmand, DcsShortWrite0_T1) {
    uint8_t tbuf[1];
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                nullptr, 0,
                                                true, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_DCS_SHORT_WRITE_0);
    EXPECT_OK(status);
}

TEST(CreateCommmand, GenShortWrite2_T1) {
    uint8_t tbuf[2];
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                nullptr, 0,
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_SHORT_WRITE_2);
    EXPECT_OK(status);
}

TEST(CreateCommmand, DcsShortWrite1_T1) {
    uint8_t tbuf[2];
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                nullptr, 0,
                                                true, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_DCS_SHORT_WRITE_1);
    EXPECT_OK(status);
}

TEST(CreateCommmand, GenLongWrite_T1) {
    uint8_t tbuf[4];
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                nullptr, 0,
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_LONG_WRITE);
    EXPECT_OK(status);
}

TEST(CreateCommmand, DcsLongWrite_T1) {
    uint8_t tbuf[4];
    mipi_dsi_cmd_t cmd;
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                nullptr, 0,
                                                true, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_DCS_LONG_WRITE);
    EXPECT_OK(status);
}

TEST(CreateCommmand, GenShortRead0_T1) {
    mipi_dsi_cmd_t cmd;
    uint8_t rbuf[2];
    zx_status_t status = MipiDsi::CreateCommand(nullptr, 0,
                                                rbuf, sizeof(rbuf),
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_SHORT_READ_0);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK) == MIPI_DSI_CMD_FLAGS_ACK);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) == MIPI_DSI_CMD_FLAGS_SET_MAX);
    EXPECT_OK(status);
}

TEST(CreateCommmand, GenShortRead1_T1) {
    mipi_dsi_cmd_t cmd;
    uint8_t tbuf[1];
    uint8_t rbuf[2];
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                rbuf, sizeof(rbuf),
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_SHORT_READ_1);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK) == MIPI_DSI_CMD_FLAGS_ACK);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) == MIPI_DSI_CMD_FLAGS_SET_MAX);
    EXPECT_OK(status);
}

TEST(CreateCommmand, DcsShortRead0_T1) {
    mipi_dsi_cmd_t cmd;
    uint8_t tbuf[1];
    uint8_t rbuf[2];
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                rbuf, sizeof(rbuf),
                                                true, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_DCS_READ_0);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK) == MIPI_DSI_CMD_FLAGS_ACK);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) == MIPI_DSI_CMD_FLAGS_SET_MAX);
    EXPECT_OK(status);
}

TEST(CreateCommmand, GenShortRead2_T1) {
    mipi_dsi_cmd_t cmd;
    uint8_t tbuf[2];
    uint8_t rbuf[2];
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                rbuf, sizeof(rbuf),
                                                false, &cmd);
    EXPECT_TRUE(cmd.dsi_data_type == MIPI_DSI_DT_GEN_SHORT_READ_2);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK) == MIPI_DSI_CMD_FLAGS_ACK);
    EXPECT_TRUE((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) == MIPI_DSI_CMD_FLAGS_SET_MAX);
    EXPECT_OK(status);
}

TEST(CreateCommmand, InvalidDcsRead_T1) {
    mipi_dsi_cmd_t cmd;
    uint8_t tbuf[2];
    uint8_t rbuf[2];
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                rbuf, sizeof(rbuf),
                                                true, &cmd);
    EXPECT_TRUE(status == ZX_ERR_INVALID_ARGS);
}

TEST(CreateCommmand, InvalidRead_T1) {
    mipi_dsi_cmd_t cmd;
    uint8_t tbuf[3];
    uint8_t rbuf[2];
    zx_status_t status = MipiDsi::CreateCommand(tbuf, sizeof(tbuf),
                                                rbuf, sizeof(rbuf),
                                                true, &cmd);
    EXPECT_TRUE(status == ZX_ERR_INVALID_ARGS);
}

} //namespace mipi_dsi

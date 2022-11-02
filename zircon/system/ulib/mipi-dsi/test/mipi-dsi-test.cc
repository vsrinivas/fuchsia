// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mipi-dsi/mipi-dsi.h"

#include <cstdint>
#include <memory>

#include <zxtest/zxtest.h>

namespace mipi_dsi {

TEST(CreateCommand, CommandStructure) {
  mipi_dsi_cmd_t cmd;
  uint8_t tbuf[3];
  uint8_t rbuf[3];
  EXPECT_STATUS(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), rbuf, sizeof(rbuf), false, &cmd),
                ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(cmd.virt_chn_id, kMipiDsiVirtualChanId);
  EXPECT_EQ(cmd.pld_data_list, tbuf);
  EXPECT_EQ(cmd.pld_data_count, sizeof(tbuf));
  EXPECT_EQ(cmd.rsp_data_list, rbuf);
  EXPECT_EQ(cmd.rsp_data_count, sizeof(rbuf));
  EXPECT_EQ(cmd.flags, 0);
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtUnknown);
}

TEST(CreateCommand, GenShortWrite0T1) {
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(nullptr, 0, nullptr, 0, false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenShortWrite0);
}

TEST(CreateCommand, GenShortWrite0T2) {
  mipi_dsi_cmd_t cmd;
  EXPECT_STATUS(MipiDsi::CreateCommand(nullptr, 0, nullptr, 0, true, &cmd), ZX_ERR_INVALID_ARGS);
}

TEST(CreateCommand, GenShortWrite1T1) {
  uint8_t tbuf[1];
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), nullptr, 0, false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenShortWrite1);
}

TEST(CreateCommand, DcsShortWrite0T1) {
  uint8_t tbuf[1];
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), nullptr, 0, true, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtDcsShortWrite0);
}

TEST(CreateCommand, GenShortWrite2T1) {
  uint8_t tbuf[2];
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), nullptr, 0, false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenShortWrite2);
}

TEST(CreateCommand, DcsShortWrite1T1) {
  uint8_t tbuf[2];
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), nullptr, 0, true, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtDcsShortWrite1);
}

TEST(CreateCommand, GenLongWriteT1) {
  uint8_t tbuf[4];
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), nullptr, 0, false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenLongWrite);
}

TEST(CreateCommand, DcsLongWriteT1) {
  uint8_t tbuf[4];
  mipi_dsi_cmd_t cmd;
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), nullptr, 0, true, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtDcsLongWrite);
}

TEST(CreateCommand, GenShortRead0T1) {
  mipi_dsi_cmd_t cmd;
  uint8_t rbuf[2];
  EXPECT_OK(MipiDsi::CreateCommand(nullptr, 0, rbuf, sizeof(rbuf), false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenShortRead0);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK), MIPI_DSI_CMD_FLAGS_ACK);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX), MIPI_DSI_CMD_FLAGS_SET_MAX);
}

TEST(CreateCommand, GenShortRead1T1) {
  mipi_dsi_cmd_t cmd;
  uint8_t tbuf[1];
  uint8_t rbuf[2];
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), rbuf, sizeof(rbuf), false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenShortRead1);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK), MIPI_DSI_CMD_FLAGS_ACK);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX), MIPI_DSI_CMD_FLAGS_SET_MAX);
}

TEST(CreateCommand, DcsShortRead0T1) {
  mipi_dsi_cmd_t cmd;
  uint8_t tbuf[1];
  uint8_t rbuf[2];
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), rbuf, sizeof(rbuf), true, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtDcsRead0);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK), MIPI_DSI_CMD_FLAGS_ACK);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX), MIPI_DSI_CMD_FLAGS_SET_MAX);
}

TEST(CreateCommand, GenShortRead2T1) {
  mipi_dsi_cmd_t cmd;
  uint8_t tbuf[2];
  uint8_t rbuf[2];
  EXPECT_OK(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), rbuf, sizeof(rbuf), false, &cmd));
  EXPECT_EQ(cmd.dsi_data_type, kMipiDsiDtGenShortRead2);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_ACK), MIPI_DSI_CMD_FLAGS_ACK);
  EXPECT_EQ((cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX), MIPI_DSI_CMD_FLAGS_SET_MAX);
}

TEST(CreateCommand, InvalidDcsReadT1) {
  mipi_dsi_cmd_t cmd;
  uint8_t tbuf[2];
  uint8_t rbuf[2];
  EXPECT_STATUS(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), rbuf, sizeof(rbuf), true, &cmd),
                ZX_ERR_INVALID_ARGS);
}

TEST(CreateCommand, InvalidReadT1) {
  mipi_dsi_cmd_t cmd;
  uint8_t tbuf[3];
  uint8_t rbuf[2];
  EXPECT_STATUS(MipiDsi::CreateCommand(tbuf, sizeof(tbuf), rbuf, sizeof(rbuf), true, &cmd),
                ZX_ERR_INVALID_ARGS);
}

TEST(CreateCommandFidl, CommandStructure) {
  uint8_t tbuf[2];
  uint8_t rbuf[2];

  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), sizeof(rbuf), false, allocator);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_virtual_channel_id());
  ASSERT_TRUE(res->has_write_length());
  ASSERT_TRUE(res->has_dsi_data_type());
  ASSERT_TRUE(res->has_flags());
  ASSERT_TRUE(res->has_expected_read_length());

  EXPECT_EQ(kMipiDsiVirtualChanId, res->virtual_channel_id());
  EXPECT_EQ(sizeof(rbuf), res->expected_read_length());
  EXPECT_EQ(sizeof(tbuf), res->write_length());
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX, res->flags());
  EXPECT_EQ(kMipiDsiDtGenShortRead2, res->dsi_data_type());
}

TEST(CreateCommandFidl, GenShortWrite0T1) {
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(0, 0, false, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenShortWrite0, res->dsi_data_type());
}

TEST(CreateCommandFidl, GenShortWrite0T2) {
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(0, 0, true, allocator);
  EXPECT_FALSE(res.has_value());
}

TEST(CreateCommandFidl, GenShortWrite1T1) {
  uint8_t tbuf[1];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, false, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenShortWrite1, res->dsi_data_type());
}

TEST(CreateCommandFidl, DcsShortWrite0T1) {
  uint8_t tbuf[1];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, true, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtDcsShortWrite0, res->dsi_data_type());
}

TEST(CreateCommandFidl, GenShortWrite2T1) {
  uint8_t tbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, false, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenShortWrite2, res->dsi_data_type());
}

TEST(CreateCommandFidl, DcsShortWrite1T1) {
  uint8_t tbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, true, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtDcsShortWrite1, res->dsi_data_type());
}

TEST(CreateCommandFidl, GenLongWriteT1) {
  uint8_t tbuf[4];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, false, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenLongWrite, res->dsi_data_type());
}

TEST(CreateCommandFidl, DcsLongWriteT1) {
  uint8_t tbuf[4];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, true, allocator);

  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtDcsLongWrite, res->dsi_data_type());
}

TEST(CreateCommandFidl, GenShortRead0T1) {
  uint8_t rbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(0, sizeof(rbuf), false, allocator);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenShortRead0, res->dsi_data_type());
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_ACK, (res->flags() & MIPI_DSI_CMD_FLAGS_ACK));
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_SET_MAX, (res->flags() & MIPI_DSI_CMD_FLAGS_SET_MAX));
}

TEST(CreateCommandFidl, GenShortRead1T1) {
  uint8_t tbuf[1];
  uint8_t rbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), sizeof(rbuf), false, allocator);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenShortRead1, res->dsi_data_type());
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_ACK, (res->flags() & MIPI_DSI_CMD_FLAGS_ACK));
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_SET_MAX, (res->flags() & MIPI_DSI_CMD_FLAGS_SET_MAX));
}

TEST(CreateCommandFidl, DcsShortRead0T1) {
  uint8_t tbuf[1];
  uint8_t rbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), sizeof(rbuf), true, allocator);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtDcsRead0, res->dsi_data_type());
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_ACK, (res->flags() & MIPI_DSI_CMD_FLAGS_ACK));
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_SET_MAX, (res->flags() & MIPI_DSI_CMD_FLAGS_SET_MAX));
}

TEST(CreateCommandFidl, GenShortRead2T1) {
  uint8_t tbuf[2];
  uint8_t rbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), sizeof(rbuf), false, allocator);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->has_dsi_data_type());
  EXPECT_EQ(kMipiDsiDtGenShortRead2, res->dsi_data_type());
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_ACK, (res->flags() & MIPI_DSI_CMD_FLAGS_ACK));
  EXPECT_EQ(MIPI_DSI_CMD_FLAGS_SET_MAX, (res->flags() & MIPI_DSI_CMD_FLAGS_SET_MAX));
}

TEST(CreateCommandFidl, InvalidDcsReadT1) {
  uint8_t tbuf[2];
  uint8_t rbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), sizeof(rbuf), true, allocator);
  EXPECT_FALSE(res.has_value());
}

TEST(CreateCommandFidl, InvalidReadT1) {
  uint8_t tbuf[3];
  uint8_t rbuf[2];
  fidl::Arena<2048> allocator;
  std::optional res = MipiDsi::CreateCommandFidl(sizeof(tbuf), sizeof(rbuf), true, allocator);
  EXPECT_FALSE(res.has_value());
}

}  // namespace mipi_dsi

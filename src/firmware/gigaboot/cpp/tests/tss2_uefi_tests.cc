// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <efi/types.h>
#include <gtest/gtest.h>

#include "mock_boot_service.h"
#include "tss2_uefi.h"
#include "utils.h"

namespace gigaboot {
namespace {

TEST(TssUefiTest, CreateSysContext) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  ASSERT_EQ(TSS2_TCTI_MAGIC(tcti_context), kTctiUefiMagic);
  ASSERT_EQ(TSS2_TCTI_VERSION(tcti_context), kTctiUefiVersion);
}

TEST(TssUefiTest, Transmit) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  std::vector<uint8_t> command(128, 1);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);
  ASSERT_EQ(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);
  ASSERT_EQ(tcti_context->current_command_size, command.size());
  ASSERT_EQ(memcmp(tcti_context->command_buffer.data(), command.data(), command.size()), 0);
}

TEST(TssUefiTest, Receive) {
  MockStubService stub_service;
  Device image_device({"path", "image"});  // dont care
  Tcg2Device tcg2_device;
  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&tcg2_device);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  std::vector<uint8_t> command(128, 1);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);
  ASSERT_EQ(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);

  size_t size_out = command.size();
  ASSERT_EQ(
      Tss2_Tcti_Receive(opaque_tcti_context, &size_out, command.data(), TSS2_TCTI_TIMEOUT_BLOCK),
      TSS2_RC_SUCCESS);

  // Command size should be cleared.
  ASSERT_EQ(tcti_context->current_command_size, 0ULL);

  // Some command was sent.
  ASSERT_EQ(tcg2_device.last_command(), command);
}

TEST(TssUefiTest, ReceiveReturnsMaxResponseSizeOnNullBuffer) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  std::vector<uint8_t> command(128, 1);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);
  ASSERT_EQ(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);
  ASSERT_EQ(tcti_context->current_command_size, command.size());

  size_t size_out = 0;
  ASSERT_EQ(Tss2_Tcti_Receive(opaque_tcti_context, &size_out, nullptr, TSS2_TCTI_TIMEOUT_BLOCK),
            TSS2_RC_SUCCESS);

  // Command size should not be cleared.
  ASSERT_EQ(tcti_context->current_command_size, command.size());

  // Max response size should be returned
  ASSERT_EQ(size_out, static_cast<size_t>(TPM2_MAX_RESPONSE_SIZE));
}

TEST(TssUefiTest, ReceiveFailsOnNonBlockingTimeout) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  std::vector<uint8_t> command(128, 1);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);
  ASSERT_EQ(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);
  size_t size_out = command.size();
  ASSERT_NE(Tss2_Tcti_Receive(opaque_tcti_context, &size_out, command.data(), 1), TSS2_RC_SUCCESS);
}

TEST(TssUefiTest, ReceiveFailsWithoutTransmit) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  std::vector<uint8_t> command(128, 1);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);
  size_t size_out = command.size();
  ASSERT_NE(
      Tss2_Tcti_Receive(opaque_tcti_context, &size_out, command.data(), TSS2_TCTI_TIMEOUT_BLOCK),
      TSS2_RC_SUCCESS);
}

TEST(TssUefiTest, TransmitReceiveFailsOnBadContext) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);

  std::vector<uint8_t> command(128, 1);
  size_t size_out;

  // Bad magic
  TSS2_TCTI_MAGIC(tcti_context) = 0;
  ASSERT_NE(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);
  ASSERT_NE(
      Tss2_Tcti_Receive(opaque_tcti_context, &size_out, command.data(), TSS2_TCTI_TIMEOUT_BLOCK),
      TSS2_RC_SUCCESS);

  // Bad version
  TSS2_TCTI_MAGIC(tcti_context) = kTctiUefiMagic;
  TSS2_TCTI_VERSION(tcti_context) = 3;
  ASSERT_NE(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);
  ASSERT_NE(
      Tss2_Tcti_Receive(opaque_tcti_context, &size_out, command.data(), TSS2_TCTI_TIMEOUT_BLOCK),
      TSS2_RC_SUCCESS);
}

TEST(TssUefiTest, TransmitFailsOnOversize) {
  auto sys_context = Tss2UefiSysContext::Create();
  ASSERT_NE(sys_context, nullptr);
  Tss2UefiTctiContext *tcti_context = sys_context->tcti_context();
  ASSERT_NE(tcti_context, nullptr);
  TSS2_TCTI_CONTEXT *opaque_tcti_context = reinterpret_cast<TSS2_TCTI_CONTEXT *>(tcti_context);

  std::vector<uint8_t> command(TPM2_MAX_COMMAND_SIZE + 1, 1);
  ASSERT_NE(Tss2_Tcti_Transmit(opaque_tcti_context, command.size(), command.data()),
            TSS2_RC_SUCCESS);
}

}  // namespace
}  // namespace gigaboot

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The file implements the TPM Command Transmission Interface (TCTI) layer in the EFI environment
// for using TCG Software Stack

#include "tss2_uefi.h"

#include <stdio.h>

#include "tss2/tss2_rc.h"
#include "utils.h"

namespace gigaboot {
namespace {

// Up-cast a TSS2_TCTI_CONTEXT into our implementation specific `Tss2UefiTctiContext`
Tss2UefiTctiContext *CheckAndGetTctiUefiContext(TSS2_TCTI_CONTEXT *context) {
  // Make sure magic and version matches before casting.
  if (context && TSS2_TCTI_MAGIC(context) == kTctiUefiMagic &&
      TSS2_TCTI_VERSION(context) == kTctiUefiVersion) {
    return reinterpret_cast<Tss2UefiTctiContext *>(context);
  }
  return nullptr;
}

TSS2_RC TctiUefiTransmit(TSS2_TCTI_CONTEXT *context, size_t size, const uint8_t *command) {
  // UEFI TPM 2 interface is synchronous/blocking. Thus, for transmit, we will just copy the command
  // to an internal buffer. Actual transmission and execution of the command will be done in the
  // receive callback.
  Tss2UefiTctiContext *uefi_context = CheckAndGetTctiUefiContext(context);
  if (!uefi_context) {
    return TSS2_TCTI_RC_BAD_CONTEXT;
  }

  if (size > uefi_context->command_buffer.size()) {
    return TSS2_TCTI_RC_BAD_VALUE;
  }

  memcpy(uefi_context->command_buffer.data(), command, size);
  uefi_context->current_command_size = size;
  return TSS2_RC_SUCCESS;
}

TSS2_RC TcTiUefiReceive(TSS2_TCTI_CONTEXT *context, size_t *size, uint8_t *response,
                        int32_t timeout) {
  Tss2UefiTctiContext *uefi_context = CheckAndGetTctiUefiContext(context);
  if (!uefi_context) {
    return TSS2_TCTI_RC_BAD_CONTEXT;
  }

  if (uefi_context->current_command_size == 0) {
    // No command. Transmit has not been called yet.
    return TSS2_TCTI_RC_BAD_SEQUENCE;
  }

  // UEFI TPM2 protocol is blocking. Timeout is not supported.
  if (timeout != TSS2_TCTI_TIMEOUT_BLOCK) {
    return TSS2_TCTI_RC_NOT_IMPLEMENTED;
  }

  // When response buffer is nullptr, callback shall return the expected response size.
  // If multi-chunk read is not supported, the maximum possible size is returned.
  if (response == nullptr) {
    // Another approach is to use the maximum response size obtained from GetCapability() TCG EFI
    // service, which is typically smaller.
    *size = TPM2_MAX_RESPONSE_SIZE;
    return TSS2_RC_SUCCESS;
  }

  auto tpm2_protocol = gigaboot::EfiLocateProtocol<efi_tcg2_protocol>();
  if (tpm2_protocol.is_error()) {
    return TSS2_TCTI_RC_GENERAL_FAILURE;
  }

  ZX_ASSERT(uefi_context->current_command_size <= TPM2_MAX_COMMAND_SIZE);
  efi_status status = tpm2_protocol->SubmitCommand(
      tpm2_protocol.value().get(), static_cast<uint32_t>(uefi_context->current_command_size),
      uefi_context->command_buffer.data(), static_cast<uint32_t>(*size), response);
  if (status != EFI_SUCCESS) {
    printf("Failed to submit command: %s\n", EfiStatusToString(status));
    return TSS2_TCTI_RC_GENERAL_FAILURE;
  }

  uefi_context->current_command_size = 0;
  return TSS2_RC_SUCCESS;
}

void TcTiUefiFinalize(TSS2_TCTI_CONTEXT *context) {
  // Nothing need to be done.
}

}  // namespace

std::unique_ptr<Tss2UefiTctiContext> CreateTss2UefiTctiContext() {
  auto context = std::make_unique<Tss2UefiTctiContext>();
  TSS2_TCTI_MAGIC(context.get()) = kTctiUefiMagic;
  TSS2_TCTI_VERSION(context.get()) = kTctiUefiVersion;

  // Another approach is to use the maximum command size obtained from GetCapability() TCG EFI
  // service, which is typically smaller.
  context->command_buffer.resize(TPM2_MAX_COMMAND_SIZE);

  TSS2_TCTI_TRANSMIT(context.get()) = TctiUefiTransmit;
  TSS2_TCTI_RECEIVE(context.get()) = TcTiUefiReceive;
  TSS2_TCTI_FINALIZE(context.get()) = TcTiUefiFinalize;

  // The following are optional callbacks and not applicable to UEFI environment.
  TSS2_TCTI_CANCEL(context.get()) = nullptr;
  TSS2_TCTI_GET_POLL_HANDLES(context.get()) = nullptr;
  TSS2_TCTI_SET_LOCALITY(context.get()) = nullptr;
  TSS2_TCTI_MAKE_STICKY(context.get()) = nullptr;

  return context;
}

std::unique_ptr<Tss2UefiSysContext> Tss2UefiSysContext::Create() {
  auto context = std::make_unique<Tss2UefiSysContext>();

  // Passing 0 to `Tss2_Sys_GetContextSize` will compute the sys context size assuming
  // TPM2_MAX_COMMAND_SIZE
  size_t size = Tss2_Sys_GetContextSize(0);
  context->sys_context_.resize(size);

  // Use the matching version from the current TSS library version.
  TSS2_ABI_VERSION abi_version = TSS2_ABI_VERSION_CURRENT;

  context->tcti_context_ = CreateTss2UefiTctiContext();
  if (!context->tcti_context_) {
    return nullptr;
  }

  // Initialize the TSS sys context given the TCTI context.
  TSS2_RC ret = Tss2_Sys_Initialize(
      context->sys_context(), size,
      reinterpret_cast<TSS2_TCTI_CONTEXT *>(context->tcti_context_.get()), &abi_version);
  if (ret != TSS2_RC_SUCCESS) {
    const char *err_msg = Tss2_RC_Decode(ret);
    printf("Failed to initialize SYS context: 0x%x, %s\n", ret, err_msg);
    return nullptr;
  }

  return context;
}

}  // namespace gigaboot

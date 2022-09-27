// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/lib/fuchsia-tcti/include/fuchsia-tcti.h"

#include <fidl/fuchsia.tpm/cpp/wire.h>
#include <fuchsia/tpm/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

#include <fbl/auto_lock.h>

#include "fidl/fuchsia.tpm/cpp/markers.h"

/// FuchsiaTctiContext defines the internal context that we cast the |opaque_context|
/// to on each call to fuchsia_tpm_send and fuchsia_tpm_recv. This is because of how
/// the TCG C library works it requires an opaque C pointer which it will return to
/// the TCTI on each call to either send/recv.
struct FuchsiaTctiContext {
  // The service that implements the fuchsia.tpm.Command protocol isn't designed to
  // hold individual components received commands. Instead it is up to the component
  // to do its own book keeping. This buffer is filled with any raw bytes returned by
  // Transmit and returned to the user on subsequent calls to
  // fuchsia_tpm_recv.
  std::vector<uint8_t> recv_buffer;
  /// We retain a client connection to the service for the lifetime of the context.
  fidl::WireSyncClient<fuchsia_tpm::Command> command_service;
  // It is important that concurrent sends/recv do not execute at the same time. This
  // is because we are writing to the recv_buffer and std::vector doesn't support
  // concurrent access.
  fbl::Mutex mutex;
};

opaque_ctx_t* fuchsia_tpm_init(void) {
  auto command = component::Connect<fuchsia_tpm::Command>();
  if (command.is_error()) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.tpm.Transmit protocol.";
    return nullptr;
  }
  fidl::WireSyncClient command_service{std::move(*command)};
  if (!command_service.is_valid()) {
    FX_LOGS(ERROR) << "fuchsia.tpm.Transmit protocol is not valid.";
    return nullptr;
  }
  FuchsiaTctiContext* context = new FuchsiaTctiContext();
  context->command_service = std::move(command_service);
  return context;
}

int fuchsia_tpm_send(opaque_ctx_t* opaque_context, int command_code, const uint8_t* in_buffer,
                     size_t len) {
  if (opaque_context == nullptr || in_buffer == nullptr || len == 0 ||
      len > fuchsia_tpm::wire::kMaxTpmCommandLen) {
    return 1;
  }
  FuchsiaTctiContext* context = static_cast<FuchsiaTctiContext*>(opaque_context);
  fbl::AutoLock auto_lock(&context->mutex);

  std::vector<uint8_t> command_copy(in_buffer, in_buffer + len);
  auto result = context->command_service->Transmit(
      fidl::VectorView<uint8_t>::FromExternal(command_copy.data(), command_copy.size()));
  if (!result.ok()) {
    FX_LOGS(ERROR) << "Failed to send command: " << result.error();
    return 1;
  }

  // Stash any data returned by the TransmitCommand method into the |recv_buffer|. Any
  // error should exit immediately with the error code in TPM_RC format.
  if (result.value().is_error()) {
    FX_LOGS(ERROR) << "Failed to execute command" << result.value().error_value();
    return result.value().error_value();
  }
  auto response = *result.value();
  if (response->data.count() > 0) {
    context->recv_buffer.insert(context->recv_buffer.end(), response->data.begin(),
                                response->data.end());
  }
  return 0;
}

size_t fuchsia_tpm_recv(opaque_ctx_t* opaque_context, uint8_t* out_buffer, size_t len) {
  if (opaque_context == nullptr || out_buffer == nullptr || len == 0) {
    return 0;
  }
  FuchsiaTctiContext* context = static_cast<FuchsiaTctiContext*>(opaque_context);
  fbl::AutoLock auto_lock(&context->mutex);

  // Only extract at most the length of the available buffer. fuchsia_tpm_recv returns
  // the size of bytes read so it is always valid to return less than the requested size.
  size_t bytes_to_read = std::min(len, context->recv_buffer.size());
  for (size_t i = 0; i < bytes_to_read; i++) {
    out_buffer[i] = context->recv_buffer[i];
  }
  std::vector<uint8_t>(context->recv_buffer.begin() + bytes_to_read, context->recv_buffer.end())
      .swap(context->recv_buffer);
  return bytes_to_read;
}

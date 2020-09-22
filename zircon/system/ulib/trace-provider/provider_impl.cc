// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "provider_impl.h"

#include <fuchsia/tracing/provider/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/coding.h>
#include <lib/zx/process.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include "export.h"
#include "session.h"
#include "utils.h"

namespace trace {
namespace internal {

constexpr bool kVerboseTraceErrors = false;

TraceProviderImpl::TraceProviderImpl(async_dispatcher_t* dispatcher, zx::channel channel)
    : dispatcher_(dispatcher), connection_(this, std::move(channel)) {}

TraceProviderImpl::~TraceProviderImpl() = default;

void TraceProviderImpl::Initialize(trace_buffering_mode_t buffering_mode, zx::vmo buffer,
                                   zx::fifo fifo, std::vector<std::string> categories) {
  Session::InitializeEngine(dispatcher_, buffering_mode, std::move(buffer), std::move(fifo),
                            std::move(categories));
}

void TraceProviderImpl::Start(trace_start_mode_t start_mode,
                              std::vector<std::string> additional_categories) {
  // TODO(fxbug.dev/22973): Add support for additional categories.
  Session::StartEngine(start_mode);
}

void TraceProviderImpl::Stop() { Session::StopEngine(); }

void TraceProviderImpl::Terminate() { Session::TerminateEngine(); }

void TraceProviderImpl::OnClose() { Terminate(); }

TraceProviderImpl::Connection::Connection(TraceProviderImpl* impl, zx::channel channel)
    : impl_(impl),
      channel_(std::move(channel)),
      wait_(this, channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {
  zx_status_t status = wait_.Begin(impl_->dispatcher_);
  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: begin wait failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    Close();
  }
}

TraceProviderImpl::Connection::~Connection() { Close(); }

void TraceProviderImpl::Connection::Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    // The wait could be canceled if we're shutting down, e.g., the
    // program is exiting.
    return;
  }

  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: wait failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
  } else if (signal->observed & ZX_CHANNEL_READABLE) {
    if (ReadMessage()) {
      zx_status_t status = wait_.Begin(dispatcher);
      if (status == ZX_OK) {
        return;
      }
      fprintf(stderr, "TraceProvider: Error re-registering channel wait: status=%d(%s)\n", status,
              zx_status_get_string(status));
    } else {
      fprintf(stderr, "TraceProvider: received invalid FIDL message or failed to send reply\n");
    }
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  }

  Close();
}

bool TraceProviderImpl::Connection::ReadMessage() {
  FIDL_ALIGNDECL uint8_t buffer[16 * 1024];
  uint32_t num_bytes = 0u;
  constexpr uint32_t kNumHandles = 2;
  zx_handle_t handles[kNumHandles];
  uint32_t num_handles = 0u;
  zx_status_t status =
      channel_.read(0u, buffer, handles, sizeof(buffer), kNumHandles, &num_bytes, &num_handles);
  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: channel read failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    return false;
  }

  if (!DecodeAndDispatch(buffer, num_bytes, handles, num_handles)) {
    fprintf(stderr, "TraceProvider: DecodeAndDispatch failed\n");
    zx_handle_close_many(handles, num_handles);
    return false;
  }

  return true;
}

bool TraceProviderImpl::Connection::DecodeAndDispatch(uint8_t* buffer, uint32_t num_bytes,
                                                      zx_handle_t* handles, uint32_t num_handles) {
  if (num_bytes < sizeof(fidl_message_header_t)) {
    return false;
  }

  auto hdr = reinterpret_cast<fidl_message_header_t*>(buffer);
  uint64_t ordinal = hdr->ordinal;
  switch (ordinal) {
    case fuchsia_tracing_provider_ProviderInitializeOrdinal: {
      zx_status_t status = fidl_decode(&fuchsia_tracing_provider_ProviderInitializeRequestTable,
                                       buffer, num_bytes, handles, num_handles, nullptr);
      if (status != ZX_OK) {
        return false;
      }

      auto request = reinterpret_cast<fuchsia_tracing_provider_ProviderInitializeRequest*>(buffer);
      const fuchsia_tracing_provider_ProviderConfig& config = request->config;
      auto buffering_mode = config.buffering_mode;
      auto buffer = zx::vmo(config.buffer);
      auto fifo = zx::fifo(config.fifo);
      std::vector<std::string> categories;
      auto strings = reinterpret_cast<fidl_string_t*>(config.categories.data);
      for (size_t i = 0; i < config.categories.count; i++) {
        categories.push_back(std::string(strings[i].data, strings[i].size));
      }
      trace_buffering_mode_t trace_buffering_mode;
      switch (buffering_mode) {
        case fuchsia_tracing_provider_BufferingMode_ONESHOT:
          trace_buffering_mode = TRACE_BUFFERING_MODE_ONESHOT;
          break;
        case fuchsia_tracing_provider_BufferingMode_CIRCULAR:
          trace_buffering_mode = TRACE_BUFFERING_MODE_CIRCULAR;
          break;
        case fuchsia_tracing_provider_BufferingMode_STREAMING:
          trace_buffering_mode = TRACE_BUFFERING_MODE_STREAMING;
          break;
        default:
          return false;
      }
      impl_->Initialize(trace_buffering_mode, std::move(buffer), std::move(fifo),
                        std::move(categories));
      return true;
    }
    case fuchsia_tracing_provider_ProviderStartOrdinal: {
      zx_status_t status = fidl_decode(&fuchsia_tracing_provider_ProviderStartRequestTable, buffer,
                                       num_bytes, handles, num_handles, nullptr);
      if (status != ZX_OK) {
        return false;
      }

      auto request = reinterpret_cast<fuchsia_tracing_provider_ProviderStartRequest*>(buffer);
      const fuchsia_tracing_provider_StartOptions& options = request->options;
      trace_start_mode_t start_mode;
      switch (options.buffer_disposition) {
        case fuchsia_tracing_provider_BufferDisposition_CLEAR_ENTIRE:
          start_mode = TRACE_START_CLEAR_ENTIRE_BUFFER;
          break;
        case fuchsia_tracing_provider_BufferDisposition_CLEAR_NONDURABLE:
          start_mode = TRACE_START_CLEAR_NONDURABLE_BUFFER;
          break;
        case fuchsia_tracing_provider_BufferDisposition_RETAIN:
          start_mode = TRACE_START_RETAIN_BUFFER;
          break;
        default:
          return false;
      }
      std::vector<std::string> categories;
      auto strings = reinterpret_cast<fidl_string_t*>(options.additional_categories.data);
      for (size_t i = 0; i < options.additional_categories.count; i++) {
        categories.push_back(std::string(strings[i].data, strings[i].size));
      }
      impl_->Start(start_mode, std::move(categories));
      return true;
    }
    case fuchsia_tracing_provider_ProviderStopOrdinal: {
      zx_status_t status = fidl_decode(&fuchsia_tracing_provider_ProviderStopRequestTable, buffer,
                                       num_bytes, handles, num_handles, nullptr);
      if (status != ZX_OK) {
        return false;
      }

      impl_->Stop();
      return true;
    }
    case fuchsia_tracing_provider_ProviderTerminateOrdinal: {
      zx_status_t status = fidl_decode(&fuchsia_tracing_provider_ProviderTerminateRequestTable,
                                       buffer, num_bytes, handles, num_handles, nullptr);
      if (status != ZX_OK) {
        return false;
      }

      impl_->Terminate();
      return true;
    }
    default:
      return false;
  }  // switch
}

void TraceProviderImpl::Connection::Close() {
  if (channel_) {
    wait_.Cancel();
    channel_.reset();
    impl_->OnClose();
  }
}

}  // namespace internal
}  // namespace trace

EXPORT trace_provider_t* trace_provider_create_with_name(zx_handle_t to_service_h,
                                                         async_dispatcher_t* dispatcher,
                                                         const char* name) {
  zx::channel to_service(to_service_h);

  ZX_DEBUG_ASSERT(to_service.is_valid());
  ZX_DEBUG_ASSERT(dispatcher);

  // Create the channel to which we will bind the trace provider.
  zx::channel provider_client;
  zx::channel provider_service;
  zx_status_t status = zx::channel::create(0u, &provider_client, &provider_service);
  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: channel create failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    return nullptr;
  }

  // Register the trace provider.
  status = fuchsia_tracing_provider_RegistryRegisterProvider(
      to_service.get(), provider_client.release(), trace::internal::GetPid(), name, strlen(name));
  if (status != ZX_OK) {
    if (trace::internal::kVerboseTraceErrors) {
      fprintf(stderr, "TraceProvider: registry failed: status=%d(%s)\n", status,
              zx_status_get_string(status));
    }
    return nullptr;
  }
  // Note: |to_service| can be closed now. Let it close as a consequence
  // of going out of scope.

  return new trace::internal::TraceProviderImpl(dispatcher, std::move(provider_service));
}

EXPORT trace_provider_t* trace_provider_create(zx_handle_t to_service,
                                               async_dispatcher_t* dispatcher) {
  auto self = zx::process::self();
  char name[ZX_MAX_NAME_LEN];
  auto status = self->get_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: error getting process name: status=%d(%s)\n", status,
            zx_status_get_string(status));
    name[0] = '\0';
  }
  return trace_provider_create_with_name(to_service, dispatcher, name);
}

EXPORT trace_provider_t* trace_provider_create_synchronously(zx_handle_t to_service_h,
                                                             async_dispatcher_t* dispatcher,
                                                             const char* name,
                                                             bool* out_manager_is_tracing_already) {
  zx::channel to_service(to_service_h);

  ZX_DEBUG_ASSERT(to_service.is_valid());
  ZX_DEBUG_ASSERT(dispatcher);

  // Create the channel to which we will bind the trace provider.
  zx::channel provider_client;
  zx::channel provider_service;
  zx_status_t status = zx::channel::create(0u, &provider_client, &provider_service);
  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: channel create failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    return nullptr;
  }

  // Register the trace provider.
  zx_status_t registry_status;
  bool manager_is_tracing_already;
  status = fuchsia_tracing_provider_RegistryRegisterProviderSynchronously(
      to_service.get(), provider_client.release(), trace::internal::GetPid(), name, strlen(name),
      &registry_status, &manager_is_tracing_already);
  if (status != ZX_OK) {
    fprintf(stderr, "TraceProvider: RegisterProviderSynchronously failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    return nullptr;
  }
  if (registry_status != ZX_OK) {
    fprintf(stderr, "TraceProvider: registry failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
    return nullptr;
  }
  // Note: |to_service| can be closed now. Let it close as a consequence
  // of going out of scope.

  if (out_manager_is_tracing_already)
    *out_manager_is_tracing_already = manager_is_tracing_already;
  return new trace::internal::TraceProviderImpl(dispatcher, std::move(provider_service));
}

EXPORT void trace_provider_destroy(trace_provider_t* provider) {
  ZX_DEBUG_ASSERT(provider);

  // The provider's dispatcher may be running on a different thread. This happens when, e.g., the
  // dispatcher is running in a background thread and we are called in the foreground thread.
  // async::WaitBase, which we use, requires all calls be made on the dispatcher thread. Thus we
  // can't delete |provider| here. Instead we schedule it to be deleted on the dispatcher's thread.
  //
  // There are two cases to be handled:
  // 1) The dispatcher's thread is our thread.
  // 2) The dispatcher's thread is a different thread.
  // In both cases there's an additional wrinkle:
  // a) The task we post is run.
  // b) The task we post is not run.
  // In cases (1a,2a) we're ok: The provider is deleted. The provider isn't destroyed immediately
  // but that's ok, it will be shortly.
  // In cases (1b,2b) we're also ok. The only time this happens is if the loop is shutdown before
  // our task is run. This is ok because when this happens our WaitBase method cannot be running.
  //
  // While one might want to check whether we're running in a different thread from the dispatcher
  // with dispatcher == async_get_default_dispatcher(), we don't do this as we don't assume the
  // default dispatcher has been set.

  auto raw_provider_impl = static_cast<trace::internal::TraceProviderImpl*>(provider);
  std::unique_ptr<trace::internal::TraceProviderImpl> provider_impl(raw_provider_impl);
  async::PostTask(raw_provider_impl->dispatcher(), [provider_impl = std::move(provider_impl)]() {
    // The provider will be deleted when the closure is deleted.
  });
}

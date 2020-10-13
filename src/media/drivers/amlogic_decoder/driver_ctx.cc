// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_ctx.h"

#include <lib/fdio/directory.h>
#include <lib/media/codec_impl/codec_metrics.h>
#include <stdarg.h>
#include <stdio.h>

#include <ddk/driver.h>

#include "amlogic-video.h"
#include "macros.h"

namespace {

extern "C" {

zx_status_t amlogic_video_init(void** out_ctx);
zx_status_t amlogic_video_bind(void* ctx, zx_device_t* parent);
}

extern zx_status_t amlogic_video_init(void** out_ctx) {
  DriverCtx* driver_ctx = new DriverCtx();
  *out_ctx = reinterpret_cast<void*>(driver_ctx);
  return ZX_OK;
}

// ctx is the driver ctx (not device ctx)
zx_status_t amlogic_video_bind(void* ctx, zx_device_t* parent) {
  DriverCtx* driver = reinterpret_cast<DriverCtx*>(ctx);
  auto device = std::make_unique<DeviceCtx>(driver);

  AmlogicVideo* video = device->video();

  zx_status_t status = video->InitRegisters(parent);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to initialize registers");
    return status;
  }

  status = video->InitDecoder();
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to initialize decoder");
    return status;
  }

  status = device->Bind(parent);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to bind device");
    return status;
  }

  // The pointer to DeviceCtx is add_device() ctx now, so intentionally don't
  // destruct the DeviceCtx instance.
  //
  // At least for now, the DeviceCtx stays allocated for the life of the
  // devhost process.
  device.release();
  zxlogf(INFO, "[amlogic_video_bind] bound");
  return ZX_OK;
}

}  // namespace

DriverCtx::DriverCtx() {
  // We use kAsyncLoopConfigNoAttachToCurrentThread here, because we don't really want
  // to be setting the default async_t for the thread that creates the
  // DriverCtx.  We'll plumb async_t(s) explicitly instead.
  shared_fidl_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  shared_fidl_loop_->StartThread("shared_fidl_thread", &shared_fidl_thread_);
  // This won't actually be logged until codec_factory opens a device and calls
  // SetAuxServiceDirectory() on it.  Until then we're buffering event counts.
  metrics_.LogEvent(
      media_metrics::StreamProcessorEventsMetricDimensionImplementation_AmlogicDecoderShared,
      media_metrics::StreamProcessorEventsMetricDimensionEvent_HostProcessStart);
}

DriverCtx::~DriverCtx() {
  assert(shared_fidl_loop_);
  shared_fidl_loop_->Quit();
  shared_fidl_loop_->JoinThreads();
  shared_fidl_loop_->Shutdown();
}

// TODO(dustingreen): Do format, printf, log, maybe some epitaphs.
void DriverCtx::FatalError(const char* format, ...) {
  // Let's not have a buffer on the stack, not because it couldn't be done
  // safely, but because we'd potentially run into stack size vs. message length
  // tradeoffs, stack expansion granularity fun, or whatever else.

  va_list args;
  va_start(args, format);
  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;
  va_end(args);

  // ~buffer never actually runs since this method never returns
  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  va_start(args, format);
  size_t buffer_bytes_2 = vsnprintf(buffer.get(), buffer_bytes, format, args) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  assert(buffer_bytes == buffer_bytes_2);
  va_end(args);

  DECODE_ERROR("DriverCtx::FatalError(): %s", buffer.get());

  // TODO(dustingreen): Send string in buffer via channel epitaphs, when
  // possible. The channel activity/failing server-side generally will race with
  // trying to send epitaph - probably requires enlisting shared_fidl_thread()
  // from here - probably a timeout here would be a good idea if so.

  // This should provide more stack dump than exit(-1) would give.
  ZX_ASSERT_MSG(false, "DriverCtx::FatalError() is fatal.");
}

// Run to_run on given dispatcher, in order.
void DriverCtx::PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  if (post_result != ZX_OK) {
    FatalError("async::PostTask() failed - result: %d", post_result);
  }
}

// Run to_run_on_shared_fidl_thread on shared_fidl_thread().
void DriverCtx::PostToSharedFidl(fit::closure to_run_on_shared_fidl_thread) {
  // Switch the implementation here to fit::function when possible.
  PostSerial(shared_fidl_loop()->dispatcher(), std::move(to_run_on_shared_fidl_thread));
}

void DriverCtx::SetAuxServiceDirectory(
    fidl::InterfaceHandle<fuchsia::io::Directory> aux_service_directory) {
  if (aux_service_directory_) {
    // Check if old aux_service_directory_ is PEER_CLOSED / broken.
    fidl::InterfaceHandle<fuchsia::io::Directory> tmp_directory;
    zx_status_t status = aux_service_directory_->CloneChannel(tmp_directory.NewRequest());
    if (status == ZX_OK) {
      // Keep the service directory we already had, and that seems to not be PEER_CLOSED.  This path
      // is to avoid switching to a temporary service directory when running tests that spawn their
      // own separate instance of codec_factory.
      return;
    }
    // Toss the PEER_CLOSED / broken service directory.
    aux_service_directory_ = nullptr;
  }
  ZX_DEBUG_ASSERT(!aux_service_directory_);
  aux_service_directory_ =
      std::make_shared<sys::ServiceDirectory>(std::move(aux_service_directory));
  ZX_DEBUG_ASSERT(aux_service_directory_);
  metrics_.SetServiceDirectory(aux_service_directory_);
}

CodecMetrics& DriverCtx::metrics() { return metrics_; }

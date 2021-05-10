// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "radarutil.h"

#include <fuchsia/hardware/radar/llcpp/fidl.h>
#include <lib/async/time.h>
#include <lib/fidl/llcpp/fidl_allocator.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/auto_lock.h>

namespace radarutil {

zx::status<zx::duration> ParseDuration(char* arg) {
  char* endptr;
  const int64_t duration = strtol(arg, &endptr, 10);
  if (endptr == arg || *endptr == '\0' || duration < 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  switch (*endptr) {
    case 'h':
      return zx::ok(zx::hour(duration));
    case 'm':
      if (strncmp("ms", endptr, 2) == 0) {
        return zx::ok(zx::msec(duration));
      }
      return zx::ok(zx::min(duration));
    case 's':
      return zx::ok(zx::sec(duration));
    case 'u':
      return zx::ok(zx::usec(duration));
    case 'n':
      return zx::ok(zx::nsec(duration));
  }

  return zx::error(ZX_ERR_INVALID_ARGS);
}

void Usage() {
  constexpr char kUsageString[] =
      "Usage: radarutil [-h] [-p burst process time] [-t run time] [-v vmos]\n"
      "    burst process time: Time to sleep after each burst to simulate processing delay."
      " Default: 0s\n"
      "    run time: Total time to read frames."
      " Default: 1s\n"
      "    vmos: Number of VMOs to register for receiving frames."
      " Default: 10\n"
      "\n"
      "    For time arguments, add a suffix (h,m,s,ms,us,ns) to indicate units.\n"
      "    For example: radarutil -p 3ms -t 5m -v 20\n";

  fprintf(stderr, "%s", kUsageString);
}

zx_status_t RadarUtil::Run(
    int argc, char** argv,
    fidl::ClientEnd<fuchsia_hardware_radar::RadarBurstReaderProvider> device) {
  RadarUtil radarutil;

  zx_status_t status = radarutil.ParseArgs(argc, argv);
  if (status != ZX_OK || radarutil.help_) {
    return status;
  }

  if ((status = radarutil.ConnectToDevice(std::move(device))) != ZX_OK) {
    return status;
  }

  if ((status = radarutil.RegisterVmos()) != ZX_OK) {
    return status;
  }

  if ((status = radarutil.Run()) != ZX_OK) {
    return status;
  }

  return radarutil.UnregisterVmos();
}

zx_status_t RadarUtil::ParseArgs(int argc, char** argv) {
  int opt, vmos;
  while ((opt = getopt(argc, argv, "hp:t:v:")) != -1) {
    switch (opt) {
      case 'h':
        Usage();
        help_ = true;
        return ZX_OK;
      case 'p': {
        zx::status<zx::duration> burst_process_time = ParseDuration(optarg);
        if (burst_process_time.is_error()) {
          Usage();
          return burst_process_time.error_value();
        }
        burst_process_time_ = burst_process_time.value();
        break;
      }
      case 't': {
        zx::status<zx::duration> run_time = ParseDuration(optarg);
        if (run_time.is_error()) {
          Usage();
          return run_time.error_value();
        }
        run_time_ = run_time.value();
        break;
      }
      case 'v':
        vmos = atoi(optarg);
        if (vmos <= 0) {
          Usage();
          return ZX_ERR_INVALID_ARGS;
        }
        vmo_count_ = vmos;
        break;
      default:
        Usage();
        return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}

zx_status_t RadarUtil::ConnectToDevice(fidl::ClientEnd<BurstReaderProvider> device) {
  zx_status_t status = loop_.StartThread("radarutil-client-thread");
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to start client thread: %s\n", zx_status_get_string(status));
    return status;
  }

  fidl::ClientEnd<BurstReader> client_end;
  fidl::ServerEnd<BurstReader> server_end;
  status = zx::channel::create(0, &client_end.channel(), &server_end.channel());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create channel: %s\n", zx_status_get_string(status));
    return status;
  }

  std::shared_ptr<EventHandler> event_handler(new EventHandler(this));
  client_.Bind(std::move(client_end), loop_.dispatcher(), std::move(event_handler));

  fidl::WireSyncClient<BurstReaderProvider> provider_client(std::move(device));
  auto result = provider_client.Connect(std::move(server_end));
  if (!result.ok()) {
    fprintf(stderr, "Failed to connect to radar device: %s\n",
            zx_status_get_string(result.status()));
    return result.status();
  }
  if (result->result.is_err()) {
    fprintf(stderr, "Radar device failed to bind: %u\n", result->result.err());
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t RadarUtil::RegisterVmos() {
  const auto burst_size = client_->GetBurstSize_Sync();
  if (!burst_size.ok()) {
    fprintf(stderr, "Failed to get burst size: %s\n", zx_status_get_string(burst_size.status()));
    return burst_size.status();
  }

  fidl::FidlAllocator allocator;

  std::vector<zx::vmo> vmos(vmo_count_);

  fidl::VectorView<zx::vmo> vmo_dups(allocator, vmo_count_);
  fidl::VectorView<uint32_t> vmo_ids(allocator, vmo_count_);

  zx_status_t status;
  for (uint32_t i = 0; i < vmo_count_; i++) {
    if ((status = zx::vmo::create(burst_size->burst_size, 0, &vmos[i])) != ZX_OK) {
      fprintf(stderr, "Failed to create VMO: %s\n", zx_status_get_string(status));
      return status;
    }

    if ((status = vmos[i].duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dups[i])) != ZX_OK) {
      fprintf(stderr, "Failed to duplicate VMO: %s\n", zx_status_get_string(status));
      return status;
    }

    vmo_ids[i] = i;
  }

  const auto result = client_->RegisterVmos_Sync(vmo_ids, vmo_dups);
  if (!result.ok()) {
    fprintf(stderr, "Failed to register VMOs: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  if (result->result.is_err()) {
    fprintf(stderr, "Failed to register VMOs: %d\n", result->result.err());
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t RadarUtil::UnregisterVmos() {
  const auto burst_size = client_->GetBurstSize_Sync();
  if (!burst_size.ok()) {
    fprintf(stderr, "Failed to get burst size: %s\n", zx_status_get_string(burst_size.status()));
    return burst_size.status();
  }

  fidl::FidlAllocator allocator;

  fidl::VectorView<uint32_t> vmo_ids(allocator, vmo_count_);

  for (uint32_t i = 0; i < vmo_count_; i++) {
    vmo_ids[i] = i;
  }

  const auto result = client_->UnregisterVmos_Sync(vmo_ids);
  if (!result.ok()) {
    fprintf(stderr, "Failed to register VMOs: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  if (result->result.is_err()) {
    fprintf(stderr, "Failed to register VMOs: %d\n", result->result.err());
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t RadarUtil::Run() {
  thrd_t worker_thread;
  int thread_status = thrd_create_with_name(
      &worker_thread,
      [](void* ctx) -> int { return reinterpret_cast<RadarUtil*>(ctx)->WorkerThread(); }, this,
      "radarutil-worker-thread");
  if (thread_status != thrd_success) {
    zx_status_t status = thrd_status_to_zx_status(thread_status);
    fprintf(stderr, "Failed to start worker thread: %s\n", zx_status_get_string(status));
    return status;
  }

  {
    const auto result = client_->StartBursts();
    if (!result.ok()) {
      fprintf(stderr, "Failed to start bursts: %s\n", zx_status_get_string(result.status()));
      return result.status();
    }
  }

  zx::nanosleep(zx::deadline_after(run_time_));

  {
    const auto result = client_->StopBursts_Sync();
    if (!result.ok()) {
      fprintf(stderr, "Failed to stop bursts: %s\n", zx_status_get_string(result.status()));
      return result.status();
    }
  }

  run_ = false;
  worker_event_.Broadcast();

  thrd_join(worker_thread, nullptr);

  printf("Received %lu bursts and %lu burst errors\n", bursts_received_, burst_errors_);

  return ZX_OK;
}

int RadarUtil::WorkerThread() {
  while (run_) {
    fbl::AutoLock lock(&lock_);
    while (burst_vmo_ids_.empty() && run_) {
      worker_event_.Wait(&lock_);
    }

    while (!burst_vmo_ids_.empty()) {
      const uint32_t vmo_id = burst_vmo_ids_.front();
      burst_vmo_ids_.pop();
      bursts_received_++;

      if (burst_process_time_.to_nsecs() > 0) {
        zx::nanosleep(zx::deadline_after(burst_process_time_));
      }

      client_->UnlockVmo(vmo_id);
    }
  }

  return thrd_success;
}

void RadarUtil::OnBurst(fidl::WireResponse<BurstReader::OnBurst>* event) {
  if (event->result.is_response()) {
    {
      fbl::AutoLock lock(&lock_);
      burst_vmo_ids_.push(event->result.response().burst.vmo_id);
    }
    worker_event_.Broadcast();
  }
  if (event->result.is_err()) {
    burst_errors_++;
  }
}

}  // namespace radarutil

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"

#include "magma_system_connection.h"
#include "magma_util/macros.h"
#include "platform_handle.h"
#include "platform_object.h"

uint32_t MagmaSystemDevice::GetDeviceId() {
  uint64_t result;
  magma::Status status = Query(MAGMA_QUERY_DEVICE_ID, &result);
  if (!status.ok())
    return 0;

  DASSERT(result >> 32 == 0);
  return static_cast<uint32_t>(result);
}

std::shared_ptr<magma::PlatformConnection> MagmaSystemDevice::Open(
    std::shared_ptr<MagmaSystemDevice> device, msd_client_id_t client_id,
    std::unique_ptr<magma::PlatformHandle> thread_profile,
    std::unique_ptr<magma::PlatformHandle> server_endpoint,
    std::unique_ptr<magma::PlatformHandle> server_notification_endpoint) {
  msd_connection_t* msd_connection = msd_device_open(device->msd_dev(), client_id);
  if (!msd_connection)
    return DRETP(nullptr, "msd_device_open failed");

  return magma::PlatformConnection::Create(
      std::make_unique<MagmaSystemConnection>(std::move(device),
                                              MsdConnectionUniquePtr(msd_connection)),
      client_id, std::move(thread_profile), std::move(server_endpoint),
      std::move(server_notification_endpoint));
}

void MagmaSystemDevice::StartConnectionThread(
    std::shared_ptr<magma::PlatformConnection> platform_connection) {
  std::unique_lock<std::mutex> lock(connection_list_mutex_);

  auto shutdown_event = platform_connection->ShutdownEvent();
  std::thread thread(magma::PlatformConnection::RunLoop, std::move(platform_connection));

  connection_map_->insert(std::pair<std::thread::id, Connection>(
      thread.get_id(), Connection{std::move(thread), std::move(shutdown_event)}));
}

void MagmaSystemDevice::ConnectionClosed(std::thread::id thread_id) {
  std::unique_lock<std::mutex> lock(connection_list_mutex_);

  if (!connection_map_)
    return;

  auto iter = connection_map_->find(thread_id);
  // May not be in the map if no connection thread was started.
  if (iter != connection_map_->end()) {
    iter->second.thread.detach();
    connection_map_->erase(iter);
  }
}

void MagmaSystemDevice::Shutdown() {
  std::unique_lock<std::mutex> lock(connection_list_mutex_);
  auto map = std::move(connection_map_);
  lock.unlock();

  for (auto& element : *map) {
    element.second.shutdown_event->Signal();
  }

  auto start = std::chrono::high_resolution_clock::now();

  for (auto& element : *map) {
    element.second.thread.join();
  }

  std::chrono::duration<double, std::milli> elapsed =
      std::chrono::high_resolution_clock::now() - start;
  DLOG("shutdown took %u ms", (uint32_t)elapsed.count());

  (void)elapsed;
}

void MagmaSystemDevice::SetMemoryPressureLevel(MagmaMemoryPressureLevel level) {
  msd_device_set_memory_pressure_level(msd_dev(), level);
}

magma::Status MagmaSystemDevice::Query(uint64_t id, magma_handle_t* result_buffer_out,
                                       uint64_t* result_out) {
  switch (id) {
    case MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS:
      *result_out = magma::PlatformConnection::kMaxInflightMessages;
      *result_out <<= 32;
      *result_out |= magma::PlatformConnection::kMaxInflightMemoryMB;
      return MAGMA_STATUS_OK;
  }
  return msd_device_query(msd_dev(), id, result_buffer_out, result_out);
}

magma_status_t MagmaSystemDevice::GetIcdList(std::vector<msd_icd_info_t>* icd_list_out) {
  icd_list_out->clear();
  uint64_t list_size;
  magma_status_t status = msd_device_get_icd_list(msd_dev(), 0, nullptr, &list_size);
  if (status != MAGMA_STATUS_OK)
    return DRET(status);
  icd_list_out->resize(list_size);
  status =
      msd_device_get_icd_list(msd_dev(), icd_list_out->size(), icd_list_out->data(), &list_size);
  if (status != MAGMA_STATUS_OK)
    return DRET(status);
  DASSERT(list_size == icd_list_out->size());
  return MAGMA_STATUS_OK;
}

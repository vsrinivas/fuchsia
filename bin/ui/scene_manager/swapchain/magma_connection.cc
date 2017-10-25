// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/swapchain/magma_connection.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "lib/fxl/logging.h"

namespace scene_manager {
namespace {
// TODO(MZ-386): Don't hardcode display name.
const char* kDeviceName = "/dev/class/display/000";
}  // namespace

MagmaConnection::MagmaConnection() : fd_(-1), conn_(nullptr) {}

MagmaConnection::~MagmaConnection() {
  if (conn_ != nullptr) {
    magma_release_connection(conn_);
  }

  if (fd_ >= 0) {
    close(fd_);
  }
}

bool MagmaConnection::Open() {
  fd_ = open(kDeviceName, O_RDONLY);
  if (fd_ < 0) {
    FXL_LOG(ERROR) << "Failed to open display device: " << kDeviceName << ".";
    return false;
  }

  conn_ = magma_create_connection(fd_, MAGMA_CAPABILITY_DISPLAY);
  if (conn_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to open magma connection";
    close(fd_);
    return false;
  }

  return true;
}

bool MagmaConnection::GetDisplaySize(uint32_t* width, uint32_t* height) {
  magma_status_t status;
  magma_display_size display_size;
  status = magma_display_get_size(fd_, &display_size);
  if (status != MAGMA_STATUS_OK) {
    FXL_LOG(ERROR) << "Failed to get display size: " << status << ".";
    return false;
  }

  *width = display_size.width;
  *height = display_size.height;
  return true;
}

bool MagmaConnection::ImportBuffer(const zx::vmo& vmo_handle,
                                   magma_buffer_t* buffer) {
  magma_status_t status;
  status = magma_import(conn_, vmo_handle.get(), buffer);
  return status == MAGMA_STATUS_OK;
}

void MagmaConnection::FreeBuffer(magma_buffer_t buffer) {
  magma_release_buffer(conn_, buffer);
}

bool MagmaConnection::CreateSemaphore(magma_semaphore_t* sem) {
  magma_status_t status;
  status = magma_create_semaphore(conn_, sem);
  return status == MAGMA_STATUS_OK;
}

bool MagmaConnection::ImportSemaphore(const zx::event& event,
                                      magma_semaphore_t* sem) {
  magma_status_t status;
  status = magma_import_semaphore(conn_, event.get(), sem);
  return status == MAGMA_STATUS_OK;
}

void MagmaConnection::ReleaseSemaphore(magma_semaphore_t sem) {
  magma_release_semaphore(conn_, sem);
}

void MagmaConnection::SignalSemaphore(magma_semaphore_t sem) {
  magma_signal_semaphore(sem);
}

void MagmaConnection::ResetSemaphore(magma_semaphore_t sem) {
  magma_reset_semaphore(sem);
}

bool MagmaConnection::DisplayPageFlip(
    magma_buffer_t buffer,
    uint32_t wait_semaphore_count,
    const magma_semaphore_t* wait_semaphores,
    uint32_t signal_semaphore_count,
    const magma_semaphore_t* signal_semaphores,
    magma_semaphore_t buffer_presented_semaphore) {
  magma_status_t status = magma_display_page_flip(
      conn_, buffer, wait_semaphore_count, wait_semaphores,
      signal_semaphore_count, signal_semaphores, buffer_presented_semaphore);
  if (status != MAGMA_STATUS_OK) {
    FXL_LOG(ERROR) << "Failed to do page flip.";
    return false;
  }
  return true;
}

}  // namespace scene_manager

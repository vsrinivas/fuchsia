// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONNECTION_H
#define MSD_INTEL_CONNECTION_H

#include <list>
#include <memory>

#include "command_buffer.h"
#include "engine_command_streamer.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "msd_intel_pci_device.h"

class MsdIntelContext;

class MsdIntelConnection {
 public:
  class Owner : public PerProcessGtt::Owner {
   public:
    virtual ~Owner() = default;

    virtual void SubmitBatch(std::unique_ptr<MappedBatch> batch) = 0;
    virtual void DestroyContext(std::shared_ptr<MsdIntelContext> client_context) = 0;
  };

  static std::unique_ptr<MsdIntelConnection> Create(Owner* owner, msd_client_id_t client_id);

  virtual ~MsdIntelConnection() {}

  std::shared_ptr<PerProcessGtt> per_process_gtt() { return ppgtt_; }

  msd_client_id_t client_id() { return client_id_; }

  void SubmitBatch(std::unique_ptr<MappedBatch> batch) { owner_->SubmitBatch(std::move(batch)); }

  static std::shared_ptr<MsdIntelContext> CreateContext(
      std::shared_ptr<MsdIntelConnection> connection);

  void DestroyContext(std::shared_ptr<MsdIntelContext> context);

  void SetNotificationCallback(msd_connection_notification_callback_t callback, void* token) {
    notifications_.Set(callback, token);
  }

  // Called by the device thread when command buffers complete.
  void SendNotification(const std::vector<uint64_t>& buffer_ids) {
    notifications_.SendBufferIds(buffer_ids);
  }

  void SendContextKilled() {
    notifications_.SendContextKilled();
    sent_context_killed_ = true;
  }

  void AddHandleWait(msd_connection_handle_wait_complete_t completer,
                     msd_connection_handle_wait_start_t starter, void* wait_context,
                     magma_handle_t handle) {
    notifications_.AddHandleWait(completer, starter, wait_context, handle);
  }
  void CancelHandleWait(void* cancel_token) { notifications_.CancelHandleWait(cancel_token); }

  // Maps |page_count| pages of the given |buffer| at |page_offset| to |gpu_addr| into the
  // GPU address space belonging to this connection.
  magma::Status MapBufferGpu(std::shared_ptr<MsdIntelBuffer> buffer, uint64_t gpu_addr,
                             uint64_t page_offset, uint64_t page_count);

  void ReleaseBuffer(magma::PlatformBuffer* buffer);

 private:
  MsdIntelConnection(Owner* owner, std::shared_ptr<PerProcessGtt> ppgtt, msd_client_id_t client_id)
      : owner_(owner), ppgtt_(std::move(ppgtt)), client_id_(client_id) {}

  bool sent_context_killed() { return sent_context_killed_; }

  // The given callback should return when any of the given semaphores are signaled.
  void ReleaseBuffer(
      magma::PlatformBuffer* buffer,
      std::function<magma::Status(
          std::vector<std::shared_ptr<magma::PlatformSemaphore>>& semaphores, uint32_t timeout_ms)>
          wait_callback);

  static const uint32_t kMagic = 0x636f6e6e;  // "conn" (Connection)

  Owner* owner_;
  std::shared_ptr<PerProcessGtt> ppgtt_;
  msd_client_id_t client_id_;
  bool sent_context_killed_ = false;
  std::list<std::shared_ptr<MsdIntelContext>> context_list_;

  class Notifications {
   public:
    void SendBufferIds(const std::vector<uint64_t>& buffer_ids) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (callback_ && token_) {
        msd_notification_t notification = {.type = MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND};
        const uint32_t max = MSD_CHANNEL_SEND_MAX_SIZE / sizeof(uint64_t);

        uint32_t dst_index = 0;
        for (uint32_t src_index = 0; src_index < buffer_ids.size();) {
          reinterpret_cast<uint64_t*>(notification.u.channel_send.data)[dst_index++] =
              buffer_ids[src_index++];
          if (dst_index == max || src_index == buffer_ids.size()) {
            notification.u.channel_send.size = dst_index * sizeof(uint64_t);
            dst_index = 0;
            callback_(token_, &notification);
          }
        }
      }
    }

    void SendContextKilled() {
      std::lock_guard<std::mutex> lock(mutex_);
      if (callback_ && token_) {
        msd_notification_t notification = {.type = MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED};
        callback_(token_, &notification);
      }
    }

    void AddHandleWait(msd_connection_handle_wait_complete_t completer,
                       msd_connection_handle_wait_start_t starter, void* wait_context,
                       magma_handle_t handle) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (callback_ && token_) {
        msd_notification_t notification = {.type = MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT};
        notification.u.handle_wait = {.starter = starter,
                                      .completer = completer,
                                      .wait_context = wait_context,
                                      .handle = handle};
        callback_(token_, &notification);
      }
    }

    void CancelHandleWait(void* cancel_token) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (callback_ && token_) {
        msd_notification_t notification = {.type = MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT_CANCEL};
        notification.u.handle_wait_cancel = {.cancel_token = cancel_token};
        callback_(token_, &notification);
      }
    }

    void Set(msd_connection_notification_callback_t callback, void* token) {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_ = callback;
      token_ = token;
    }

   private:
    msd_connection_notification_callback_t callback_ = nullptr;
    void* token_ = nullptr;
    std::mutex mutex_;
  };

  Notifications notifications_;

  friend class TestMsdIntelConnection;
};

class MsdIntelAbiConnection : public msd_connection_t {
 public:
  MsdIntelAbiConnection(std::shared_ptr<MsdIntelConnection> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdIntelAbiConnection* cast(msd_connection_t* connection) {
    DASSERT(connection);
    DASSERT(connection->magic_ == kMagic);
    return static_cast<MsdIntelAbiConnection*>(connection);
  }

  std::shared_ptr<MsdIntelConnection> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdIntelConnection> ptr_;
  static const uint32_t kMagic = 0x636f6e6e;  // "conn" (Connection)
};

#endif  // MSD_INTEL_CONNECTION_H

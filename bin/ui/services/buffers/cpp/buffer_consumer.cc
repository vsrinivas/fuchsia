// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/buffers/cpp/buffer_consumer.h"

#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"

namespace mozart {
namespace {

class ConsumedVmo : public mtl::SharedVmo {
 public:
  explicit ConsumedVmo(mx::vmo vmo,
                       uint32_t map_flags,
                       mx_koid_t koid,
                       std::weak_ptr<ConsumedBufferRegistry> weak_registry);
  ~ConsumedVmo() override;

  void Release();
  bool AddRetention(mx_koid_t retention_koid, mx::eventpair retention);
  void RemoveRetention(mx_koid_t retention_koid);

 private:
  mx_koid_t koid_;
  std::weak_ptr<ConsumedBufferRegistry> weak_registry_;
  std::unordered_map<mx_koid_t, mx::eventpair> retentions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ConsumedVmo);
};

struct RetentionInfo {
  mtl::MessageLoop::HandlerKey handler_key;
  ftl::RefPtr<mtl::SharedVmo> shared_vmo;
  std::unordered_map<mx_koid_t, mx::eventpair> retention;
};

}  // namespace

struct ConsumedBufferRegistry {
  // Maps VMO koids to instances.  The registry does not retain ownership of
  // these instances because they are retained by clients of the registry.
  std::mutex mutex;
  std::unordered_map<mx_koid_t, mtl::SharedVmo*> vmos;
};

BufferConsumer::BufferConsumer(uint32_t map_flags)
    : map_flags_(map_flags),
      registry_(std::make_shared<ConsumedBufferRegistry>()) {}

BufferConsumer::~BufferConsumer() {
  for (auto& pair : retained_buffers_) {
    const RetentionInfo& retention_info = pair.second;
    mtl::MessageLoop::GetCurrent()->RemoveHandler(retention_info.handler_key);
    static_cast<ConsumedVmo*>(retention_info.shared_vmo.get())->Release();
  }
}

std::unique_ptr<ConsumedBufferHolder> BufferConsumer::ConsumeBuffer(
    BufferPtr buffer) {
  if (!buffer)
    return nullptr;

  auto shared_vmo = GetSharedVmo(std::move(buffer->vmo));
  if (!shared_vmo)
    return nullptr;

  mx::eventpair retention = std::move(buffer->retention);
  if (retention) {
    mx_handle_t retention_handle = retention.get();
    mx_koid_t retention_koid = mtl::GetKoid(retention_handle);
    if (retention_koid == MX_KOID_INVALID)
      return nullptr;

    auto consumed_vmo = static_cast<ConsumedVmo*>(shared_vmo.get());
    if (consumed_vmo->AddRetention(retention_koid, std::move(retention))) {
      mtl::MessageLoop::HandlerKey handler_key =
          mtl::MessageLoop::GetCurrent()->AddHandler(this, retention_handle,
                                                     MX_SIGNAL_PEER_CLOSED);
      retained_buffers_.emplace(
          retention_handle,
          RetentionInfo{handler_key, shared_vmo, retention_koid});
    }
  }

  std::unique_ptr<BufferFence> fence;
  if (buffer->fence)
    fence.reset(new BufferFence(std::move(buffer->fence)));

  return std::unique_ptr<ConsumedBufferHolder>(
      new ConsumedBufferHolder(std::move(shared_vmo), std::move(fence)));
}

ftl::RefPtr<mtl::SharedVmo> BufferConsumer::GetSharedVmo(mx::vmo vmo) {
  if (!vmo)
    return nullptr;

  mx_koid_t vmo_koid = mtl::GetKoid(vmo.get());
  if (vmo_koid == MX_KOID_INVALID)
    return nullptr;

  std::lock_guard<std::mutex> lock(registry_->mutex);
  auto it = registry_->vmos.find(vmo_koid);
  if (it != registry_->vmos.end())
    return ftl::Ref(it->second);

  auto instance = ftl::MakeRefCounted<ConsumedVmo>(std::move(vmo), map_flags_,
                                                   vmo_koid, registry_);
  registry_->vmos.emplace(vmo_koid, instance.get());
  return instance;
}

void BufferConsumer::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(pending & MX_SIGNAL_PEER_CLOSED);

  auto it = retained_buffers_.find(handle);
  FTL_DCHECK(it != retained_buffers_.end());

  // Remove the retention from the associated VMO.
  const RetentionInfo& retention_info = it->second;
  mtl::MessageLoop::GetCurrent()->RemoveHandler(retention_info.handler_key);
  static_cast<ConsumedVmo*>(retention_info.shared_vmo.get())
      ->RemoveRetention(retention_info.retention_koid);

  retained_buffers_.erase(it);
}

void BufferConsumer::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_CHECK(false) << "A handle error occurred while waiting, this should"
                      "never happen: error="
                   << error;
}

ConsumedBufferHolder::ConsumedBufferHolder(
    ftl::RefPtr<mtl::SharedVmo> shared_vmo,
    std::unique_ptr<BufferFence> fence)
    : shared_vmo_(std::move(shared_vmo)), fence_(std::move(fence)) {
  FTL_DCHECK(shared_vmo_);
}

ConsumedBufferHolder::~ConsumedBufferHolder() {}

ConsumedVmo::ConsumedVmo(mx::vmo vmo,
                         uint32_t map_flags,
                         mx_koid_t koid,
                         std::weak_ptr<ConsumedBufferRegistry> weak_registry)
    : SharedVmo(std::move(vmo), map_flags),
      koid_(koid),
      weak_registry_(std::move(weak_registry)) {}

ConsumedVmo::~ConsumedVmo() {
  auto registry = weak_registry_.lock();
  if (registry) {
    std::lock_guard<std::mutex> lock(registry->mutex);
    registry->vmos.erase(koid_);
  }
}

void ConsumedVmo::Release() {
  retentions_.clear();
}

bool ConsumedVmo::AddRetention(mx_koid_t retention_koid,
                               mx::eventpair retention) {
  if (retentions_.find(retention_koid) == retentions_.end()) {
    retentions_.emplace(retention_koid, std::move(retention));
    return true;
  }
  return false;
}

void ConsumedVmo::RemoveRetention(mx_koid_t retention_koid) {
  FTL_DCHECK(retentions_.find(retention_koid) != retentions_.end());
  retentions_.erase(retention_koid);
}

}  // namespace mozart

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/buffers/cpp/buffer_consumer.h"

#include <atomic>

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"

namespace mozart {
namespace {

std::atomic<int32_t> g_consumed_buffer_count;
std::atomic<int64_t> g_consumed_buffer_total_bytes;

void TraceConsumedBufferTally(int32_t count_delta, int64_t total_bytes_delta) {
  int32_t count = g_consumed_buffer_count.fetch_add(count_delta,
                                                    std::memory_order_relaxed) +
                  count_delta;
  int64_t total_bytes = g_consumed_buffer_total_bytes.fetch_add(
                            total_bytes_delta, std::memory_order_relaxed) +
                        total_bytes_delta;
  TRACE_COUNTER("gfx", "BufferConsumer/alloc", 0u, "consumed_buffers", count);
  TRACE_COUNTER("gfx", "BufferConsumer/size", 0u, "total_bytes", total_bytes);
}

class ConsumedVmo : public mtl::SharedVmo {
 public:
  explicit ConsumedVmo(mx::vmo vmo,
                       uint32_t map_flags,
                       mx_koid_t vmo_koid,
                       std::weak_ptr<ConsumedBufferRegistry> weak_registry);
  ~ConsumedVmo() override;

  void Release();
  bool AddRetention(mx_koid_t retention_koid, mx::eventpair retention);
  void RemoveRetention(mx_koid_t retention_koid);

 private:
  mx_koid_t vmo_koid_;
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

// Maps VMO koids to instances.  The registry does not retain ownership of
// these instances because they are retained by clients of the registry.
class ConsumedBufferRegistry {
 public:
  ConsumedBufferRegistry();
  ~ConsumedBufferRegistry();

  static std::shared_ptr<ConsumedBufferRegistry> Create() {
    auto shared = std::make_shared<ConsumedBufferRegistry>();
    shared->weak_ = shared;
    return shared;
  }

  ftl::RefPtr<mtl::SharedVmo> GetSharedVmo(mx::vmo vmo, uint32_t map_flags);
  void ReleaseVmo(mx_koid_t vmo_koid);

 private:
  std::weak_ptr<ConsumedBufferRegistry> weak_;

  std::mutex mutex_;
  std::unordered_map<mx_koid_t, mtl::SharedVmo*> vmos_;
};

BufferConsumer::BufferConsumer(uint32_t map_flags)
    : map_flags_(map_flags), registry_(ConsumedBufferRegistry::Create()) {}

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

  auto shared_vmo = registry_->GetSharedVmo(std::move(buffer->vmo), map_flags_);
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
                                                     MX_EPAIR_PEER_CLOSED);
      retained_buffers_.emplace(
          retention_handle,
          RetentionInfo{handler_key, shared_vmo, retention_koid});
      TracePooledBufferCount();
    }
  }

  std::unique_ptr<BufferFence> fence;
  if (buffer->fence)
    fence.reset(new BufferFence(std::move(buffer->fence)));

  return std::unique_ptr<ConsumedBufferHolder>(
      new ConsumedBufferHolder(std::move(shared_vmo), std::move(fence)));
}

void BufferConsumer::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(pending & MX_EPAIR_PEER_CLOSED);

  auto it = retained_buffers_.find(handle);
  FTL_DCHECK(it != retained_buffers_.end());

  // Remove the retention from the associated VMO.
  const RetentionInfo& retention_info = it->second;
  mtl::MessageLoop::GetCurrent()->RemoveHandler(retention_info.handler_key);
  static_cast<ConsumedVmo*>(retention_info.shared_vmo.get())
      ->RemoveRetention(retention_info.retention_koid);
  retained_buffers_.erase(it);
  TracePooledBufferCount();
}

void BufferConsumer::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_CHECK(false) << "A handle error occurred while waiting, this should"
                      "never happen: error="
                   << error;
}

void BufferConsumer::TracePooledBufferCount() const {
  TRACE_COUNTER("gfx", "BufferConsumer/pool",
                 reinterpret_cast<uintptr_t>(this), "retained_buffers",
                 retained_buffers_.size());
}

ConsumedBufferHolder::ConsumedBufferHolder(
    ftl::RefPtr<mtl::SharedVmo> shared_vmo,
    std::unique_ptr<BufferFence> fence)
    : shared_vmo_(std::move(shared_vmo)), fence_(std::move(fence)) {
  FTL_DCHECK(shared_vmo_);
}

ConsumedBufferHolder::~ConsumedBufferHolder() {}

ConsumedBufferRegistry::ConsumedBufferRegistry() {}

ConsumedBufferRegistry::~ConsumedBufferRegistry() {}

ftl::RefPtr<mtl::SharedVmo> ConsumedBufferRegistry::GetSharedVmo(
    mx::vmo vmo,
    uint32_t map_flags) {
  if (!vmo)
    return nullptr;

  mx_koid_t vmo_koid = mtl::GetKoid(vmo.get());
  if (vmo_koid == MX_KOID_INVALID)
    return nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = vmos_.find(vmo_koid);
  if (it != vmos_.end())
    return ftl::Ref(it->second);

  auto instance = ftl::MakeRefCounted<ConsumedVmo>(std::move(vmo), map_flags,
                                                   vmo_koid, weak_);
  vmos_.emplace(vmo_koid, instance.get());
  return instance;
}

void ConsumedBufferRegistry::ReleaseVmo(mx_koid_t vmo_koid) {
  std::lock_guard<std::mutex> lock(mutex_);
  vmos_.erase(vmo_koid);
}

ConsumedVmo::ConsumedVmo(mx::vmo vmo,
                         uint32_t map_flags,
                         mx_koid_t vmo_koid,
                         std::weak_ptr<ConsumedBufferRegistry> weak_registry)
    : SharedVmo(std::move(vmo), map_flags),
      vmo_koid_(vmo_koid),
      weak_registry_(std::move(weak_registry)) {
  TraceConsumedBufferTally(1, vmo_size());
}

ConsumedVmo::~ConsumedVmo() {
  auto registry = weak_registry_.lock();
  if (registry)
    registry->ReleaseVmo(vmo_koid_);
  TraceConsumedBufferTally(-1, -static_cast<int64_t>(vmo_size()));
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

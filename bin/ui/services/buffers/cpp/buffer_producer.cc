// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/buffers/cpp/buffer_producer.h"

#include "lib/ftl/logging.h"

namespace mozart {
namespace {

// Establishes a constraint on whether a VMO should be reused for an
// allocation of the specified size, taking into account wasted space.
bool ShouldRecycle(size_t vmo_size, size_t needed_size) {
  return needed_size > vmo_size / 2;
}

// Keeps track of a VMO which was produced by a |BufferProducer| and the
// manner in which it is being retained.
class ProducedVmo : public mtl::SharedVmo {
 public:
  explicit ProducedVmo(mx::vmo vmo,
                       uint32_t map_flags,
                       mx::eventpair retainer,
                       mx::eventpair retention);
  ~ProducedVmo() override;

  const mx::eventpair& retention() const { return retention_; }

  void Release() {
    retainer_.reset();
    retention_.reset();
  }

 private:
  mx::eventpair retainer_;
  mx::eventpair retention_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ProducedVmo);
};

}  // namespace

BufferProducer::BufferProducer(uint32_t map_flags) : map_flags_(map_flags) {}

BufferProducer::~BufferProducer() {
  for (auto& pair : buffers_in_flight_) {
    const FlightInfo& flight_info = pair.second;
    mtl::MessageLoop::GetCurrent()->RemoveHandler(flight_info.handler_key);
    static_cast<ProducedVmo*>(flight_info.shared_vmo.get())->Release();
  }

  for (auto& shared_vmo : available_buffers_) {
    static_cast<ProducedVmo*>(shared_vmo.get())->Release();
  }
}

std::unique_ptr<ProducedBufferHolder> BufferProducer::ProduceBuffer(
    size_t size) {
  auto shared_vmo = GetSharedVmo(size);
  if (!shared_vmo)
    return nullptr;

  auto production_fence = std::make_shared<mx::eventpair>();
  mx::eventpair consumption_fence;
  mx_status_t status =
      mx::eventpair::create(0u, production_fence.get(), &consumption_fence);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create eventpair for fence: status=" << status;
    return nullptr;
  }

  mtl::MessageLoop::HandlerKey handler_key =
      mtl::MessageLoop::GetCurrent()->AddHandler(this, production_fence->get(),
                                                 MX_SIGNAL_PEER_CLOSED);

  buffers_in_flight_.emplace(
      production_fence->get(),
      FlightInfo{handler_key, shared_vmo, production_fence});

  return std::unique_ptr<ProducedBufferHolder>(new ProducedBufferHolder(
      std::move(shared_vmo), std::move(production_fence),
      std::move(consumption_fence)));
}

ftl::RefPtr<mtl::SharedVmo> BufferProducer::GetSharedVmo(size_t size) {
  for (auto it = available_buffers_.begin(); it != available_buffers_.end();
       ++it) {
    size_t vmo_size = (*it)->vmo_size();
    if (vmo_size >= size) {
      if (!ShouldRecycle(vmo_size, size))
        break;  // too wasteful to use a buffer of this size or larger
      auto shared_vmo = std::move(*it);
      available_buffers_.erase(it);
      return shared_vmo;
    }
  }

  return CreateSharedVmo(size);
}

ftl::RefPtr<mtl::SharedVmo> BufferProducer::CreateSharedVmo(size_t size) {
  mx::vmo vmo;
  mx_status_t status = mx::vmo::create(size, 0u, &vmo);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create vmo: status=" << status
                   << ", size=" << size;
    return nullptr;
  }

  // Optimization: We will be writing to every page of the buffer, so
  // allocate physical memory for it eagerly.
  status = vmo.op_range(MX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to commit all pages of vmo: status=" << status
                   << ", size=" << size;
    return nullptr;
  }

  mx::eventpair retainer, retention;
  status = mx::eventpair::create(0u, &retainer, &retention);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create eventpair for vmo retention: status="
                   << status;
    return nullptr;
  }

  return ftl::AdoptRef(new ProducedVmo(
      std::move(vmo), map_flags_, std::move(retainer), std::move(retention)));
}

void BufferProducer::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(pending & MX_SIGNAL_PEER_CLOSED);

  auto it = buffers_in_flight_.find(handle);
  FTL_DCHECK(it != buffers_in_flight_.end());

  // Add the newly available buffer to the pool.
  // TODO(jeffbrown): Figure out when and how to trim the pool.
  const FlightInfo& flight_info = it->second;
  mtl::MessageLoop::GetCurrent()->RemoveHandler(flight_info.handler_key);
  available_buffers_.insert(std::move(flight_info.shared_vmo));

  buffers_in_flight_.erase(it);
}

ProducedBufferHolder::ProducedBufferHolder(
    ftl::RefPtr<mtl::SharedVmo> shared_vmo,
    std::shared_ptr<mx::eventpair> production_fence,
    mx::eventpair consumption_fence)
    : shared_vmo_(std::move(shared_vmo)),
      production_fence_(std::move(production_fence)),
      consumption_fence_(std::move(consumption_fence)) {
  FTL_DCHECK(shared_vmo_);
  FTL_DCHECK(production_fence_ && *production_fence_);
  FTL_DCHECK(consumption_fence_);
}

ProducedBufferHolder::~ProducedBufferHolder() {
  SetReadySignal();
}

void ProducedBufferHolder::SetReadySignal() {
  if (ready_)
    return;

  mx_status_t status =
      production_fence_->signal_peer(0u, MX_EPAIR_SIGNALED);
  FTL_DCHECK(status == NO_ERROR);
  ready_ = true;
}

BufferPtr ProducedBufferHolder::GetBuffer(uint32_t vmo_rights) {
  auto buffer = Buffer::New();
  if (shared_vmo_->vmo().duplicate(vmo_rights, &buffer->vmo) != NO_ERROR)
    return nullptr;
  if (consumption_fence_.duplicate(
          MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ,
          &buffer->fence) != NO_ERROR)
    return nullptr;
  const mx::eventpair& retention =
      static_cast<ProducedVmo*>(shared_vmo_.get())->retention();
  if (retention.duplicate(
          MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ,
          &buffer->retention) != NO_ERROR)
    return nullptr;
  return buffer;
}

ProducedVmo::ProducedVmo(mx::vmo vmo,
                         uint32_t map_flags,
                         mx::eventpair retainer,
                         mx::eventpair retention)
    : SharedVmo(std::move(vmo), map_flags),
      retainer_(std::move(retainer)),
      retention_(std::move(retention)) {
  FTL_DCHECK(retainer_);
  FTL_DCHECK(retention_);
}

ProducedVmo::~ProducedVmo() {}

}  // namespace mozart

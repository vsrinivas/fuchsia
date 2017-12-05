// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <object/dispatcher.h>

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <lib/user_copy/user_ptr.h>

class FifoDispatcher final : public PeeredDispatcher<FifoDispatcher> {
public:
    static zx_status_t Create(uint32_t elem_count, uint32_t elem_size, uint32_t options,
                              fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1,
                              zx_rights_t* rights);

    ~FifoDispatcher() final;

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_FIFO; }
    zx_koid_t get_related_koid() const final { return peer_koid_; }
    bool has_state_tracker() const final { return true; }
    void on_zero_handles() final;
    zx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

    zx_status_t WriteFromUser(user_in_ptr<const uint8_t> src, size_t len, uint32_t* actual);
    zx_status_t ReadToUser(user_out_ptr<uint8_t> dst, size_t len, uint32_t* actual);

private:
    FifoDispatcher(fbl::RefPtr<PeerHolder<FifoDispatcher>> holder,
                   uint32_t options, uint32_t elem_count, uint32_t elem_size,
                   fbl::unique_ptr<uint8_t[]> data);
    void Init(fbl::RefPtr<FifoDispatcher> other);
    zx_status_t WriteSelf(user_in_ptr<const uint8_t> ptr, size_t len, uint32_t* actual);
    zx_status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);

    void OnPeerZeroHandles();

    fbl::Canary<fbl::magic("FIFO")> canary_;
    const uint32_t elem_count_;
    const uint32_t elem_size_;
    const uint32_t mask_;
    zx_koid_t peer_koid_;

    fbl::Mutex lock_;
    fbl::RefPtr<FifoDispatcher> other_ TA_GUARDED(lock_);
    uint32_t head_ TA_GUARDED(lock_);
    uint32_t tail_ TA_GUARDED(lock_);
    fbl::unique_ptr<uint8_t[]> data_ TA_GUARDED(lock_);

    static constexpr uint32_t kMaxSizeBytes = PAGE_SIZE;
};

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
    static zx_status_t Create(size_t elem_count, size_t elem_size, uint32_t options,
                              fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1,
                              zx_rights_t* rights);

    ~FifoDispatcher() final;

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_FIFO; }
    bool has_state_tracker() const final { return true; }

    zx_status_t WriteFromUser(size_t elem_size, user_in_ptr<const uint8_t> src, size_t count,
                              size_t* actual);
    zx_status_t ReadToUser(size_t elem_size, user_out_ptr<uint8_t> dst, size_t count,
                           size_t* actual);

    // PeeredDispatcher implementation.
    void on_zero_handles_locked() TA_REQ(get_lock());
    void OnPeerZeroHandlesLocked() TA_REQ(get_lock());

private:
    FifoDispatcher(fbl::RefPtr<PeerHolder<FifoDispatcher>> holder,
                   uint32_t options, uint32_t elem_count, uint32_t elem_size,
                   fbl::unique_ptr<uint8_t[]> data);
    void Init(fbl::RefPtr<FifoDispatcher> other);
    zx_status_t WriteSelfLocked(size_t elem_size, user_in_ptr<const uint8_t> ptr, size_t count,
                                size_t* actual) TA_REQ(get_lock());
    zx_status_t UserSignalSelfLocked(uint32_t clear_mask, uint32_t set_mask) TA_REQ(get_lock());

    fbl::Canary<fbl::magic("FIFO")> canary_;
    const uint32_t elem_count_;
    const uint32_t elem_size_;
    const uint32_t mask_;

    uint32_t head_ TA_GUARDED(get_lock());
    uint32_t tail_ TA_GUARDED(get_lock());
    fbl::unique_ptr<uint8_t[]> data_ TA_GUARDED(get_lock());

    static constexpr uint32_t kMaxSizeBytes = PAGE_SIZE;
};

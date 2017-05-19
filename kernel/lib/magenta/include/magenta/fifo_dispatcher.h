// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/canary.h>
#include <mxtl/ref_counted.h>

typedef mx_status_t (*fifo_copy_from_fn_t)(const uint8_t* ptr, uint8_t* data, size_t len);
typedef mx_status_t (*fifo_copy_to_fn_t)(uint8_t* ptr, const uint8_t* data, size_t len);

class FifoDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t elem_count, uint32_t elem_size, uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1,
                           mx_rights_t* rights);

    ~FifoDispatcher() final;

    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_FIFO; }
    mx_koid_t get_related_koid() const final { return peer_koid_; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;

    mx_status_t Write(const uint8_t* src, size_t len, uint32_t* actual);
    mx_status_t Read(uint8_t* dst, size_t len, uint32_t* actual);

    mx_status_t WriteFromUser(const uint8_t* src, size_t len, uint32_t* actual);
    mx_status_t ReadToUser(uint8_t* dst, size_t len, uint32_t* actual);

private:
    FifoDispatcher(uint32_t elem_count, uint32_t elem_size, uint32_t options);
    mx_status_t Init(mxtl::RefPtr<FifoDispatcher> other);
    mx_status_t Write(const uint8_t* ptr, size_t len, uint32_t* actual,
                      fifo_copy_from_fn_t copy_from_fn);
    mx_status_t WriteSelf(const uint8_t* ptr, size_t len, uint32_t* actual,
                          fifo_copy_from_fn_t copy_from_fn);
    mx_status_t Read(uint8_t* ptr, size_t len, uint32_t* actual,
                     fifo_copy_to_fn_t copy_to_fn);

    void OnPeerZeroHandles();

    mxtl::Canary<mxtl::magic("FIFO")> canary_;
    const uint32_t elem_count_;
    const uint32_t elem_size_;
    const uint32_t mask_;
    mx_koid_t peer_koid_;
    StateTracker state_tracker_;

    Mutex lock_;
    mxtl::RefPtr<FifoDispatcher> other_ TA_GUARDED(lock_);
    uint32_t head_ TA_GUARDED(lock_);
    uint32_t tail_ TA_GUARDED(lock_);
    uint8_t* data_ TA_GUARDED(lock_);

    static constexpr uint32_t kMaxSizeBytes = PAGE_SIZE;
};

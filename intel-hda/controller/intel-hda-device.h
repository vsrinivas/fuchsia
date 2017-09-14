// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>

#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"

#include "thread-annotations.h"

namespace audio {
namespace intel_hda {

template <typename DeviceType>
class IntelHDADevice : public DispatcherChannel::Owner {
protected:
    friend class fbl::RefPtr<IntelHDADevice>;
    IntelHDADevice() { }
    virtual ~IntelHDADevice() { }

    zx_status_t ProcessChannel(DispatcherChannel* channel) final TA_EXCL(process_lock());

    // Exported for thread analysis purposes.
    const fbl::Mutex& process_lock() const TA_RET_CAP(process_lock_) { return process_lock_; }

    void Shutdown() TA_EXCL(process_lock());

    zx_status_t DeviceIoctl(uint32_t op,
                            const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual)
        TA_EXCL(process_lock());

private:
    // TODO(johngro) : Right now, client message processing is completely
    // serialized by the process_lock_.  If we could change this to be
    // reader/writer lock instead, we could allow multiple callbacks from
    // different channels in parallel and still be able to synchronize with all
    // callback in flight by obtaining the lock exclusively.
    fbl::Mutex process_lock_;
    bool is_shutdown_ TA_GUARDED(process_lock_) = false;
};

}  // namespace intel_hda
}  // namespace audio

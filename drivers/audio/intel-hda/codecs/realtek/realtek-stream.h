// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/ref_ptr.h>

#include "drivers/audio/intel-hda/codecs/utils/stream-base.h"

#include "utils.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class RealtekStream : public IntelHDAStreamBase {
public:
    RealtekStream(const StreamProperties& props)
        : IntelHDAStreamBase(props.stream_id, props.is_input),
          props_(props) { }

protected:
    friend class mxtl::RefPtr<RealtekStream>;

    virtual ~RealtekStream() { }

    // IntelHDAStreamBase implementation
    mx_status_t OnActivateLocked()    __TA_REQUIRES(obj_lock()) final;
    void        OnDeactivateLocked()  __TA_REQUIRES(obj_lock()) final;
    mx_status_t BeginChangeStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock()) final;
    mx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        __TA_REQUIRES(obj_lock()) final;

private:
    mx_status_t RunCmdListLocked(const CommandListEntry* list, size_t count, bool force_all = false)
        __TA_REQUIRES(obj_lock());
    mx_status_t DisableConverterLocked(bool force_all = false) __TA_REQUIRES(obj_lock());

    const StreamProperties props_;
};

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda

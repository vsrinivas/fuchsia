// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>

#include <dispatcher-pool/dispatcher-channel.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>

namespace audio {
namespace intel_hda {

class IntelHDACodec;

class CodecCmdJob;
using CodecCmdJobAllocTraits = fbl::StaticSlabAllocatorTraits<fbl::unique_ptr<CodecCmdJob>>;
using CodecCmdJobAllocator = fbl::SlabAllocator<CodecCmdJobAllocTraits>;

class CodecCmdJob : public fbl::DoublyLinkedListable<fbl::unique_ptr<CodecCmdJob>>,
                    public fbl::SlabAllocated<CodecCmdJobAllocTraits> {
public:
    CodecCommand command()   const { return cmd_; }
    uint8_t      codec_id()  const { return cmd_.codec_id(); }
    uint16_t     nid()       const { return cmd_.nid(); }
    CodecVerb    verb()      const { return cmd_.verb(); }

    const fbl::RefPtr<dispatcher::Channel>& response_channel() const { return response_channel_; }
    zx_txid_t transaction_id() const { return transaction_id_; }

private:
    // Only our slab allocators is allowed to construct us, and only the
    // unique_ptrs it hands out are allowed to destroy us.
    friend CodecCmdJobAllocator;
    friend fbl::unique_ptr<CodecCmdJob>;

    CodecCmdJob(CodecCommand cmd) : cmd_(cmd) { }
    CodecCmdJob(fbl::RefPtr<dispatcher::Channel>&& response_channel,
                uint32_t transaction_id,
                CodecCommand cmd)
        : cmd_(cmd),
          transaction_id_(transaction_id),
          response_channel_(fbl::move(response_channel)) { }

    ~CodecCmdJob() = default;

    const CodecCommand cmd_;
    const zx_txid_t    transaction_id_ = IHDA_INVALID_TRANSACTION_ID;
    fbl::RefPtr<dispatcher::Channel> response_channel_ = nullptr;
};

}  // namespace intel_hda
}  // namespace audio

// Let users of the slab allocator know that the storage for the allocator is
// instantiated in a separate translation unit.
FWD_DECL_STATIC_SLAB_ALLOCATOR(::audio::intel_hda::CodecCmdJobAllocTraits);

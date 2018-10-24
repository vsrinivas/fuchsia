// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace-provider/handler.h>

#include <lib/async/cpp/wait.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zircon-internal/fnv1hash.h>

namespace trace {
namespace internal {

class Session final : public trace::TraceHandler {
public:
    static void StartEngine(async_dispatcher_t* dispatcher,
                            trace_buffering_mode_t buffering_mode,
                            zx::vmo buffer, zx::fifo fifo,
                            fbl::Vector<fbl::String> enabled_categories);
    static void StopEngine();

private:
    Session(void* buffer, size_t buffer_num_bytes, zx::fifo fifo,
                     fbl::Vector<fbl::String> enabled_categories);
    ~Session() override;

    // |trace::TraceHandler|
    bool IsCategoryEnabled(const char* category) override;
    void TraceStarted() override;
    void TraceStopped(async_dispatcher_t* dispatcher,
                      zx_status_t disposition, size_t buffer_bytes_written) override;
    // This is called in streaming mode to notify the trace manager that
    // buffer |buffer_number| is full and needs to be saved.
    void NotifyBufferFull(uint32_t wrapped_count, uint64_t durable_data_end)
        override;

    void HandleFifo(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                    zx_status_t status,
                    const zx_packet_signal_t* signal);
    bool ReadFifoMessage();

    // This is called in streaming mode when TraceManager reports back that
    // it has saved the buffer.
    static zx_status_t MarkBufferSaved(uint32_t wrapped_count,
                                       uint64_t durable_data_end);

    void* buffer_;
    size_t buffer_num_bytes_;
    zx::fifo fifo_;
    async::WaitMethod<Session, &Session::HandleFifo> fifo_wait_;
    fbl::Vector<fbl::String> const enabled_categories_;

    using CString = const char*;

    // For faster implementation of IsCategoryEnabled().
    struct StringSetEntry : public fbl::SinglyLinkedListable<
            fbl::unique_ptr<StringSetEntry>> {
        StringSetEntry(const CString& string)
            : string(string) {}

        const CString string;

        // Used by the hash table.
        static size_t GetHash(CString key) {
            return fnv1a64str(key);
        }
    };

    // We want to work with C strings here, but the default implementation
    // would just compare pointers.
    struct CategoryStringKeyTraits {
        static CString GetKey(const StringSetEntry& obj) {
            return obj.string;
        }
        static bool EqualTo(const CString& key1, const CString& key2) {
            return strcmp(key1, key2) == 0;
        }
    };

    using StringSet = fbl::HashTable<const char*,
        fbl::unique_ptr<StringSetEntry>,
        fbl::SinglyLinkedList<fbl::unique_ptr<StringSetEntry>>, // default
        size_t, // default
        37, // default
        CategoryStringKeyTraits>;
    StringSet enabled_category_set_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Session);
};

} // namespace internal
} // namespace trace

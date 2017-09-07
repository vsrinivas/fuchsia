// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace-reader/records.h>

#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

namespace trace {

class Chunk;

// Reads trace records.
class TraceReader {
public:
    // Called once for each record read by |ReadRecords|.
    // TODO(jeffbrown): It would be nice to get rid of this by making |ReadRecords|
    // return std::optional<Record> as an out parameter.
    using RecordConsumer = fbl::Function<void(Record)>;

    // Callback invoked when decoding errors are detected in the trace.
    using ErrorHandler = fbl::Function<void(fbl::String)>;

    explicit TraceReader(RecordConsumer record_consumer,
                         ErrorHandler error_handler);

    // Reads as many records as possible from the chunk, invoking the
    // record consumer for each one.  Returns true if the stream could possibly
    // contain more records if the chunk were extended with new data.
    // Returns false if the trace stream is unrecoverably corrupt and no
    // further decoding is possible.  May be called repeatedly with new
    // chunks as they become available to resume decoding.
    bool ReadRecords(Chunk& chunk);

    // Gets the current trace provider id.
    // Returns 0 if no providers have been registered yet.
    ProviderId current_provider_id() const { return current_provider_->id; }

    // Gets the name of the current trace provider.
    // Returns an empty string if the current provider id is 0.
    const fbl::String& current_provider_name() const {
        return current_provider_->name;
    }

    // Gets the name of the specified provider, or an empty string if there is
    // no such provider.
    fbl::String GetProviderName(ProviderId id) const;

private:
    bool ReadMetadataRecord(Chunk& record,
                            RecordHeader header);
    bool ReadInitializationRecord(Chunk& record,
                                  RecordHeader header);
    bool ReadStringRecord(Chunk& record,
                          RecordHeader header);
    bool ReadThreadRecord(Chunk& record,
                          RecordHeader header);
    bool ReadEventRecord(Chunk& record, RecordHeader header);
    bool ReadKernelObjectRecord(Chunk& record,
                                RecordHeader header);
    bool ReadContextSwitchRecord(Chunk& record,
                                 RecordHeader header);
    bool ReadLogRecord(Chunk& record, RecordHeader header);
    bool ReadArguments(Chunk& record,
                       size_t count,
                       fbl::Vector<Argument>* out_arguments);

    void SetCurrentProvider(ProviderId id);
    void RegisterProvider(ProviderId id, fbl::String name);
    void RegisterString(trace_string_index_t index, fbl::String string);
    void RegisterThread(trace_thread_index_t index, const ProcessThread& process_thread);

    bool DecodeStringRef(Chunk& chunk,
                         trace_encoded_string_ref_t string_ref,
                         fbl::String* out_string) const;
    bool DecodeThreadRef(Chunk& chunk,
                         trace_encoded_thread_ref_t thread_ref,
                         ProcessThread* out_process_thread) const;

    void ReportError(fbl::String error) const;

    RecordConsumer const record_consumer_;
    ErrorHandler const error_handler_;

    RecordHeader pending_header_ = 0u;

    struct StringTableEntry : public fbl::SinglyLinkedListable<
                                  fbl::unique_ptr<StringTableEntry>> {
        StringTableEntry(trace_string_index_t index,
                         fbl::String string)
            : index(index), string(fbl::move(string)) {}

        trace_string_index_t const index;
        fbl::String const string;

        // Used by the hash table.
        trace_string_index_t GetKey() const { return index; }
        static size_t GetHash(trace_string_index_t key) { return key; }
    };

    struct ThreadTableEntry : public fbl::SinglyLinkedListable<
                                  fbl::unique_ptr<ThreadTableEntry>> {
        ThreadTableEntry(trace_thread_index_t index,
                         const ProcessThread& process_thread)
            : index(index), process_thread(process_thread) {}

        trace_thread_index_t const index;
        ProcessThread const process_thread;

        // Used by the hash table.
        trace_thread_index_t GetKey() const { return index; }
        static size_t GetHash(trace_thread_index_t key) { return key; }
    };

    struct ProviderInfo : public fbl::SinglyLinkedListable<fbl::unique_ptr<ProviderInfo>> {
        ProviderId id;
        fbl::String name;

        // TODO(MG-1056): It would be more efficient to use something like
        // std::unordered_map<> here.  In particular, the table entries are
        // small enough that it doesn't make sense to heap allocate them
        // individually.
        fbl::HashTable<trace_string_index_t, fbl::unique_ptr<StringTableEntry>> string_table;
        fbl::HashTable<trace_thread_index_t, fbl::unique_ptr<ThreadTableEntry>> thread_table;

        // Used by the hash table.
        ProviderId GetKey() const { return id; }
        static size_t GetHash(ProviderId key) { return key; }
    };

    fbl::HashTable<ProviderId, fbl::unique_ptr<ProviderInfo>> providers_;
    ProviderInfo* current_provider_ = nullptr;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceReader);
};

// Provides support for reading sequences of 64-bit words from a buffer.
class Chunk final {
public:
    Chunk();
    explicit Chunk(const uint64_t* begin, size_t num_words);

    uint64_t remaining_words() const { return end_ - current_; }

    // Reads from the chunk, maintaining proper alignment.
    // Returns true on success, false if the chunk has insufficient remaining
    // words to satisfy the request.
    bool ReadUint64(uint64_t* out_value);
    bool ReadInt64(int64_t* out_value);
    bool ReadDouble(double* out_value);
    bool ReadString(size_t length, fbl::StringPiece* out_string);
    bool ReadChunk(size_t num_words, Chunk* out_chunk);

private:
    const uint64_t* current_;
    const uint64_t* end_;
};

} // namespace trace

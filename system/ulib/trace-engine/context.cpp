// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Notes on buffering modes
// ------------------------
//
// Threads and strings are cached to improve performance and reduce buffer
// usage. The caching involves emitting separate records that identify
// threads/strings and then refering to them by a numeric id. For performance
// each thread in the application maintains its own cache.
//
// Oneshot: The trace buffer is just one large buffer, and records are written
// until the buffer is full after which all further records are dropped.
//
// Circular:
// The trace buffer is effectively divided into two pieces, and tracing begins
// by writing to the first piece. Once one buffer fills we start writing to the
// other one. This results in half the buffer being dropped at every switch,
// but simplifies things because we don't have to worry about varying record
// lengths.
//
// Streaming:
// The trace buffer is effectively divided into two pieces, and tracing begins
// by writing to the first piece. Once one buffer fills we start writing to the
// other buffer, if it is available, and notify the handler that the buffer is
// full. If the other buffer is not available, then records are dropped until
// it becomes available. The other buffer is unavailable between the point when
// it filled and when the handler reports back that the buffer's contents have
// been saved.
//
// There are two important properties we wish to preserve in circular and
// streaming modes:
// 1) We don't want records describing threads and strings to be dropped:
// otherwise records refering to them will have nothing to refer to.
// 2) We don't want thread records to be dropped at all: Fidelity of recording
// of all traced threads is important, even if some of their records are
// dropped.
// To implement both (1) and (2) we introduce a third buffer that holds
// records we don't want to drop called the "durable buffer". Threads and
// small strings are recorded there. The two buffers holding normal trace
// output are called "rolling buffers", as they fill we roll from one to the
// next. Thread and string records typically aren't very large, the durable
// buffer can hold a lot of records. To keep things simple, until there's a
// compelling reason to do something more, once the durable buffer fills
// tracing effectively stops, and all further records are dropped.
// Note: The term "rolling buffer" is intended to be internal to the trace
// engine/reader/manager and is not intended to appear in public APIs
// (at least not today).
//
// The protocol between the trace engine and the handler for saving buffers in
// streaming mode is as follows:
// 1) Buffer fills -> handler gets notified via
//    |trace_handler_ops::notify_buffer_full()|. Two arguments are passed
//    along with this request:
//    |wrapped_count| - records how many times tracing has wrapped from one
//    buffer to the next, and also records the current buffer which is the one
//    needing saving. Since there are two rolling buffers, the buffer to save
//    is |wrapped_count & 1|.
//    |durable_data_end| - records how much data has been written to the
//    durable buffer thus far. This data needs to be written before data from
//    the rolling buffers is written so string and thread references work.
// 2) The handler receives the "notify_buffer_full" request.
// 3) The handler saves new durable data since the last time, saves the
//    rolling buffer, and replies back to the engine via
//    |trace_engine_mark_buffer_saved()|.
// 4) The engine receives this notification and marks the buffer as now empty.
//    The next time the engine tries to allocate space from this buffer it will
//    succeed.
// Note that the handler is free to save buffers at whatever rate it can
// manage. The protocol allows for records to be dropped if buffers can't be
// saved fast enough.

#include "context_impl.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include <fbl/algorithm.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <trace-engine/fields.h>
#include <trace-engine/handler.h>

namespace trace {
namespace {

// The cached koid of this process.
// Initialized on first use.
fbl::atomic<uint64_t> g_process_koid{ZX_KOID_INVALID};

// This thread's koid.
// Initialized on first use.
thread_local zx_koid_t tls_thread_koid{ZX_KOID_INVALID};

zx_koid_t GetKoid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetCurrentProcessKoid() {
    zx_koid_t koid = g_process_koid.load(fbl::memory_order_relaxed);
    if (unlikely(koid == ZX_KOID_INVALID)) {
        koid = GetKoid(zx_process_self());
        g_process_koid.store(koid, fbl::memory_order_relaxed); // idempotent
    }
    return koid;
}

zx_koid_t GetCurrentThreadKoid() {
    if (unlikely(tls_thread_koid == ZX_KOID_INVALID)) {
        tls_thread_koid = GetKoid(zx_thread_self());
    }
    return tls_thread_koid;
}

void GetObjectName(zx_handle_t handle, char* name_buf, size_t name_buf_size,
                   trace_string_ref* out_name_ref) {
    zx_status_t status = zx_object_get_property(handle, ZX_PROP_NAME,
                                                name_buf, name_buf_size);
    name_buf[name_buf_size - 1] = 0;
    if (status == ZX_OK) {
        *out_name_ref = trace_make_inline_c_string_ref(name_buf);
    } else {
        *out_name_ref = trace_make_empty_string_ref();
    }
}

// The next context generation number.
fbl::atomic<uint32_t> g_next_generation{1u};

// A string table entry.
struct StringEntry : public fbl::SinglyLinkedListable<StringEntry*> {
    // Attempted to assign an index.
    static constexpr uint32_t kAllocIndexAttempted = 1u << 0;
    // Successfully assigned an index.
    static constexpr uint32_t kAllocIndexSucceeded = 1u << 1;
    // Category check performed.
    static constexpr uint32_t kCategoryChecked = 1u << 2;
    // Category is enabled.
    static constexpr uint32_t kCategoryEnabled = 1u << 3;

    // The string literal itself.
    const char* string_literal;

    // Flags for the string entry.
    uint32_t flags;

    // The index with which the string was associated, or 0 if none.
    trace_string_index_t index;

    // Used by the hash table.
    const char* GetKey() const { return string_literal; }
    static size_t GetHash(const char* key) { return reinterpret_cast<uintptr_t>(key); }
};

// Cached thread and string data for a context.
// Each thread has its own cache of context state to avoid locking overhead
// while writing trace events in the common case.  There may be some
// duplicate registration of strings across threads.
struct ContextCache {
    ContextCache() = default;
    ~ContextCache() { string_table.clear(); }

    // The generation number of the context which last modified this state.
    uint32_t generation{0u};

    // Thread reference created when this thread was registered.
    trace_thread_ref_t thread_ref{};

    // Maximum number of strings to cache per thread.
    static constexpr size_t kMaxStringEntries = 256;

    // String table.
    // Provides a limited amount of storage for rapidly looking up string literals
    // registered by this thread.
    fbl::HashTable<const char*, StringEntry*> string_table;

    // Storage for the string entries.
    StringEntry string_entries[kMaxStringEntries];
};
thread_local fbl::unique_ptr<ContextCache> tls_cache{};

ContextCache* GetCurrentContextCache(uint32_t generation) {
    ContextCache* cache = tls_cache.get();
    if (likely(cache)) {
        if (likely(cache->generation == generation))
            return cache;
        if (unlikely(cache->generation > generation))
            return nullptr;
    } else {
        cache = new ContextCache();
        tls_cache.reset(cache);
    }
    cache->generation = generation;
    cache->thread_ref = trace_make_unknown_thread_ref();
    cache->string_table.clear();
    return cache;
}

StringEntry* CacheStringEntry(uint32_t generation,
                              const char* string_literal) {
    ContextCache* cache = GetCurrentContextCache(generation);
    if (unlikely(!cache))
        return nullptr;

    auto it = cache->string_table.find(string_literal);
    if (likely(it.IsValid()))
        return it.CopyPointer();

    size_t count = cache->string_table.size();
    if (unlikely(count == ContextCache::kMaxStringEntries))
        return nullptr;

    StringEntry* entry = &cache->string_entries[count];
    entry->string_literal = string_literal;
    entry->flags = 0u;
    entry->index = 0u;
    cache->string_table.insert(entry);
    return entry;
}

inline constexpr uint64_t MakeRecordHeader(RecordType type, size_t size) {
    return RecordFields::Type::Make(ToUnderlyingType(type)) |
           RecordFields::RecordSize::Make(size >> 3);
}

inline constexpr uint64_t MakeArgumentHeader(ArgumentType type, size_t size,
                                             const trace_string_ref_t* name_ref) {
    return ArgumentFields::Type::Make(ToUnderlyingType(type)) |
           ArgumentFields::ArgumentSize::Make(size >> 3) |
           ArgumentFields::NameRef::Make(name_ref->encoded_value);
}

size_t SizeOfEncodedStringRef(const trace_string_ref_t* string_ref) {
    return trace_is_inline_string_ref(string_ref)
               ? Pad(trace_inline_string_ref_length(string_ref))
               : 0u;
}

size_t SizeOfEncodedThreadRef(const trace_thread_ref_t* thread_ref) {
    // TODO(ZX-1030): Unknown thread refs should not be stored inline.
    return trace_is_inline_thread_ref(thread_ref) || trace_is_unknown_thread_ref(thread_ref)
               ? WordsToBytes(2)
               : 0u;
}

size_t SizeOfEncodedArgValue(const trace_arg_value_t* arg_value) {
    switch (arg_value->type) {
    case TRACE_ARG_NULL:
        return 0u;
    case TRACE_ARG_INT32:
        return 0u; // stored inline
    case TRACE_ARG_UINT32:
        return 0u; // stored inline
    case TRACE_ARG_INT64:
        return WordsToBytes(1);
    case TRACE_ARG_UINT64:
        return WordsToBytes(1);
    case TRACE_ARG_DOUBLE:
        return WordsToBytes(1);
    case TRACE_ARG_STRING:
        return SizeOfEncodedStringRef(&arg_value->string_value_ref);
    case TRACE_ARG_POINTER:
        return WordsToBytes(1);
    case TRACE_ARG_KOID:
        return WordsToBytes(1);
    default:
        // skip unrecognized argument type
        ZX_DEBUG_ASSERT(false);
        return 0u;
    }
}

size_t SizeOfEncodedArg(const trace_arg_t* arg) {
    return sizeof(ArgumentHeader) +
           SizeOfEncodedStringRef(&arg->name_ref) +
           SizeOfEncodedArgValue(&arg->value);
}

size_t SizeOfEncodedArgs(const trace_arg_t* args, size_t num_args) {
    size_t total_size = 0u;
    while (num_args-- != 0u)
        total_size += SizeOfEncodedArg(args++);
    return total_size;
}

// Provides support for writing sequences of 64-bit words into a trace buffer.
class Payload {
public:
    explicit Payload(trace_context_t* context, size_t num_bytes)
        : ptr_(context->AllocRecord(num_bytes)) {}

    explicit Payload(trace_context_t* context, bool rqst_durable, size_t num_bytes)
        : ptr_(rqst_durable && context->UsingDurableBuffer()
               ? context->AllocDurableRecord(num_bytes)
               : context->AllocRecord(num_bytes)) {}

    explicit operator bool() const {
        return ptr_ != nullptr;
    }

    Payload& WriteUint64(uint64_t value) {
        *ptr_++ = value;
        return *this;
    }

    Payload& WriteInt64(int64_t value) {
        *reinterpret_cast<int64_t*>(ptr_++) = value;
        return *this;
    }

    Payload& WriteDouble(double value) {
        *reinterpret_cast<double*>(ptr_++) = value;
        return *this;
    }

    void* PrepareWriteBytes(size_t length) {
        auto result = ptr_;
        ptr_ += length / 8u;
        size_t tail = length & 7u;
        if (tail) {
            size_t padding = 8u - tail;
            ptr_++;
            memset(reinterpret_cast<uint8_t*>(ptr_) - padding, 0u, padding);
        }
        return result;
    }

    Payload& WriteBytes(const void* src, size_t length) {
        auto ptr = PrepareWriteBytes(length);
        memcpy(ptr, src, length);
        return *this;
    }

    Payload& WriteStringRef(const trace_string_ref_t* string_ref) {
        if (trace_is_inline_string_ref(string_ref)) {
            WriteBytes(string_ref->inline_string,
                       trace_inline_string_ref_length(string_ref));
        }
        return *this;
    }

    Payload& WriteThreadRef(const trace_thread_ref_t* thread_ref) {
        // TODO(ZX-1030): Unknown thread refs should not be stored inline.
        if (trace_is_inline_thread_ref(thread_ref) || trace_is_unknown_thread_ref(thread_ref)) {
            WriteUint64(thread_ref->inline_process_koid);
            WriteUint64(thread_ref->inline_thread_koid);
        }
        return *this;
    }

    Payload& WriteArg(const trace_arg_t* arg) {
        switch (arg->value.type) {
        case TRACE_ARG_NULL:
            WriteArgumentHeaderAndName(ArgumentType::kNull, &arg->name_ref, 0u, 0u);
            break;
        case TRACE_ARG_INT32:
            WriteArgumentHeaderAndName(ArgumentType::kInt32, &arg->name_ref, 0u,
                                       Int32ArgumentFields::Value::Make(arg->value.int32_value));
            break;
        case TRACE_ARG_UINT32:
            WriteArgumentHeaderAndName(ArgumentType::kUint32, &arg->name_ref, 0u,
                                       Uint32ArgumentFields::Value::Make(arg->value.uint32_value));
            break;
        case TRACE_ARG_INT64:
            WriteArgumentHeaderAndName(ArgumentType::kInt64, &arg->name_ref, WordsToBytes(1), 0u);
            WriteInt64(arg->value.int64_value);
            break;
        case TRACE_ARG_UINT64:
            WriteArgumentHeaderAndName(ArgumentType::kUint64, &arg->name_ref, WordsToBytes(1), 0u);
            WriteUint64(arg->value.uint64_value);
            break;
        case TRACE_ARG_DOUBLE:
            WriteArgumentHeaderAndName(ArgumentType::kDouble, &arg->name_ref, WordsToBytes(1), 0u);
            WriteDouble(arg->value.double_value);
            break;
        case TRACE_ARG_STRING:
            WriteArgumentHeaderAndName(ArgumentType::kString, &arg->name_ref,
                                       SizeOfEncodedStringRef(&arg->value.string_value_ref),
                                       StringArgumentFields::Index::Make(
                                           arg->value.string_value_ref.encoded_value));
            WriteStringRef(&arg->value.string_value_ref);
            break;
        case TRACE_ARG_POINTER:
            WriteArgumentHeaderAndName(ArgumentType::kPointer, &arg->name_ref, WordsToBytes(1), 0u);
            WriteUint64(arg->value.pointer_value);
            break;
        case TRACE_ARG_KOID:
            WriteArgumentHeaderAndName(ArgumentType::kKoid, &arg->name_ref, WordsToBytes(1), 0u);
            WriteUint64(arg->value.koid_value);
            break;
        default:
            // skip unrecognized argument type
            ZX_DEBUG_ASSERT(false);
            break;
        }
        return *this;
    }

    Payload& WriteArgs(const trace_arg_t* args, size_t num_args) {
        while (num_args-- != 0u)
            WriteArg(args++);
        return *this;
    }

private:
    void WriteArgumentHeaderAndName(ArgumentType type,
                                    const trace_string_ref_t* name_ref,
                                    size_t content_size,
                                    uint64_t header_bits) {
        const size_t argument_size = sizeof(ArgumentHeader) +
                                     SizeOfEncodedStringRef(name_ref) +
                                     content_size;
        WriteUint64(MakeArgumentHeader(type, argument_size, name_ref) | header_bits);
        WriteStringRef(name_ref);
    }

    uint64_t* ptr_;
};

Payload WriteEventRecordBase(
    trace_context_t* context,
    EventType event_type,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args,
    size_t content_size) {
    const size_t record_size = sizeof(RecordHeader) +
                               WordsToBytes(1) +
                               SizeOfEncodedThreadRef(thread_ref) +
                               SizeOfEncodedStringRef(category_ref) +
                               SizeOfEncodedStringRef(name_ref) +
                               SizeOfEncodedArgs(args, num_args) +
                               content_size;
    Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(MakeRecordHeader(RecordType::kEvent, record_size) |
                         EventRecordFields::EventType::Make(ToUnderlyingType(event_type)) |
                         EventRecordFields::ArgumentCount::Make(num_args) |
                         EventRecordFields::ThreadRef::Make(thread_ref->encoded_value) |
                         EventRecordFields::CategoryStringRef::Make(category_ref->encoded_value) |
                         EventRecordFields::NameStringRef::Make(name_ref->encoded_value))
            .WriteUint64(event_time)
            .WriteThreadRef(thread_ref)
            .WriteStringRef(category_ref)
            .WriteStringRef(name_ref)
            .WriteArgs(args, num_args);
    }
    return payload;
}

bool CheckCategory(trace_context_t* context, const char* category) {
    return context->handler()->ops->is_category_enabled(context->handler(), category);
}

// Returns true if write succeeded, false otherwise.
// The write fails if the buffer we use is full.

bool WriteStringRecord(trace_context_t* context, bool rqst_durable_buffer,
                       trace_string_index_t index,
                       const char* string, size_t length) {
    ZX_DEBUG_ASSERT(index != TRACE_ENCODED_STRING_REF_EMPTY);
    ZX_DEBUG_ASSERT(index <= TRACE_ENCODED_STRING_REF_MAX_INDEX);

    if (unlikely(length > TRACE_ENCODED_STRING_REF_MAX_LENGTH))
        length = TRACE_ENCODED_STRING_REF_MAX_LENGTH;

    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::Pad(length);
    Payload payload(context, rqst_durable_buffer, record_size);
    if (likely(payload)) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kString, record_size) |
                         trace::StringRecordFields::StringIndex::Make(index) |
                         trace::StringRecordFields::StringLength::Make(length))
            .WriteBytes(string, length);
        return true;
    }
    return false;
}

// Returns true if write succeeded, false otherwise.
// The write fails if the buffer we use is full.

bool WriteThreadRecord(trace_context_t* context, trace_thread_index_t index,
                       zx_koid_t process_koid, zx_koid_t thread_koid) {
    ZX_DEBUG_ASSERT(index != TRACE_ENCODED_THREAD_REF_INLINE);
    ZX_DEBUG_ASSERT(index <= TRACE_ENCODED_THREAD_REF_MAX_INDEX);

    const size_t record_size =
        sizeof(trace::RecordHeader) + trace::WordsToBytes(2);
    trace::Payload payload(context, true, record_size);
    if (likely(payload)) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kThread, record_size) |
                         trace::ThreadRecordFields::ThreadIndex::Make(index))
            .WriteUint64(process_koid)
            .WriteUint64(thread_koid);
        return true;
    }
    return false;
}

// N.B. This may only return false if |check_category| is true.

bool RegisterString(trace_context_t* context,
                    const char* string_literal,
                    bool check_category,
                    trace_string_ref_t* out_ref_optional) {
    if (unlikely(!string_literal || !*string_literal)) {
        if (check_category)
            return false; // NULL and empty strings are not valid categories
        if (out_ref_optional)
            *out_ref_optional = trace_make_empty_string_ref();
        return true;
    }

    StringEntry* entry = CacheStringEntry(context->generation(), string_literal);
    if (likely(entry)) {
        // Fast path: using the thread-local cache.
        if (check_category) {
            if (unlikely(!(entry->flags & StringEntry::kCategoryChecked))) {
                if (CheckCategory(context, string_literal)) {
                    entry->flags |= StringEntry::kCategoryChecked |
                                    StringEntry::kCategoryEnabled;
                } else {
                    entry->flags |= StringEntry::kCategoryChecked;
                }
            }
            if (!(entry->flags & StringEntry::kCategoryEnabled)) {
                return false; // category disabled
            }
        }

        if (out_ref_optional) {
            if (unlikely(!(entry->flags & StringEntry::kAllocIndexAttempted))) {
                entry->flags |= StringEntry::kAllocIndexAttempted;
                size_t string_len = strlen(string_literal);
                bool rqst_durable = true;
                // If allocating an index succeeds but writing the record
                // fails, toss the index and return an inline reference. The
                // index is lost anyway, but the result won't be half-complete.
                // The subsequent write of the inlined reference will likely
                // also fail, but that's ok.
                if (likely(context->AllocStringIndex(&entry->index) &&
                           WriteStringRecord(context, rqst_durable, entry->index,
                                             string_literal, string_len))) {
                    entry->flags |= StringEntry::kAllocIndexSucceeded;
                }
            }
            if (likely(entry->flags & StringEntry::kAllocIndexSucceeded)) {
                *out_ref_optional = trace_make_indexed_string_ref(entry->index);
            } else {
                *out_ref_optional = trace_make_inline_c_string_ref(string_literal);
            }
        }
        return true;
    }

    // Slow path.
    // TODO(ZX-1035): Since we can't use the thread-local cache here, cache
    // this registered string on the trace context structure, guarded by a mutex.
    // Make sure to assign it a string index if possible instead of inlining.
    if (check_category && !CheckCategory(context, string_literal)) {
        return false; // category disabled
    }
    if (out_ref_optional) {
        *out_ref_optional = trace_make_inline_c_string_ref(string_literal);
    }
    return true;
}

} // namespace
} // namespace trace

bool trace_context_is_category_enabled(
    trace_context_t* context,
    const char* category_literal) {
    return trace::RegisterString(context, category_literal, true, nullptr);
}

void trace_context_register_string_copy(
    trace_context_t* context,
    const char* string, size_t length,
    trace_string_ref_t* out_ref) {
    // TODO(ZX-1035): Cache the registered strings on the trace context structure,
    // guarded by a mutex.
    trace_string_index_t index;
    bool rqst_durable = true;
    // If allocating an index succeeds but writing the record
    // fails, toss the index and return an inline reference. The
    // index is lost anyway, but the result won't be half-complete.
    // The subsequent write of the inlined reference will likely
    // also fail, but that's ok.
    if (likely(context->AllocStringIndex(&index) &&
               trace::WriteStringRecord(context, rqst_durable, index, string, length))) {
        *out_ref = trace_make_indexed_string_ref(index);
    } else {
        *out_ref = trace_make_inline_string_ref(string, length);
    }
}

void trace_context_register_string_literal(
    trace_context_t* context,
    const char* string_literal,
    trace_string_ref_t* out_ref) {
    bool result = trace::RegisterString(context, string_literal, false, out_ref);
    ZX_DEBUG_ASSERT(result);
}

bool trace_context_register_category_literal(
    trace_context_t* context,
    const char* category_literal,
    trace_string_ref_t* out_ref) {
    return trace::RegisterString(context, category_literal, true, out_ref);
}

void trace_context_register_current_thread(
    trace_context_t* context,
    trace_thread_ref_t* out_ref) {
    trace::ContextCache* cache = trace::GetCurrentContextCache(context->generation());
    if (likely(cache && !trace_is_unknown_thread_ref(&cache->thread_ref))) {
        // Fast path: the thread is already registered.
        *out_ref = cache->thread_ref;
        return;
    }

    trace_string_ref name_ref;
    char name_buf[ZX_MAX_NAME_LEN];
    trace::GetObjectName(zx_thread_self(), name_buf, sizeof(name_buf), &name_ref);
    zx_koid_t process_koid = trace::GetCurrentProcessKoid();
    zx_koid_t thread_koid = trace::GetCurrentThreadKoid();
    trace_context_write_thread_info_record(context, process_koid, thread_koid,
                                           &name_ref);

    if (likely(cache)) {
        trace_thread_index_t index;
        // If allocating an index succeeds but writing the record fails,
        // toss the index and return an inline reference. The index is lost
        // anyway, but the result won't be half-complete. The subsequent
        // write of the inlined reference will likely also fail, but that's ok.
        if (likely(context->AllocThreadIndex(&index) &&
                   trace::WriteThreadRecord(context, index,
                                            process_koid, thread_koid))) {
            cache->thread_ref = trace_make_indexed_thread_ref(index);
        } else {
            cache->thread_ref = trace_make_inline_thread_ref(
                process_koid, thread_koid);
        }
        *out_ref = cache->thread_ref;
        return;
    }

    // Slow path: the context's generation is out of date so we can't
    // cache anything related to the current thread.
    trace_context_register_thread(context,
                                  trace::GetCurrentProcessKoid(),
                                  trace::GetCurrentThreadKoid(),
                                  out_ref);
}

void trace_context_register_thread(
    trace_context_t* context,
    zx_koid_t process_koid, zx_koid_t thread_koid,
    trace_thread_ref_t* out_ref) {
    // TODO(ZX-1035): Since we can't use the thread-local cache here, cache
    // this registered thread on the trace context structure, guarded by a mutex.
    trace_thread_index_t index;
    // If allocating an index succeeds but writing the record fails,
    // toss the index and return an inline reference. The index is lost
    // anyway, but the result won't be half-complete. The subsequent
    // write of the inlined reference will likely also fail, but that's ok.
    if (likely(context->AllocThreadIndex(&index) &&
               trace::WriteThreadRecord(context, index, process_koid, thread_koid))) {
        *out_ref = trace_make_indexed_thread_ref(index);
    } else {
        *out_ref = trace_make_inline_thread_ref(process_koid, thread_koid);
    }
}

void trace_context_write_blob_record(
    trace_context_t* context,
    trace_blob_type_t type,
    const trace_string_ref_t* name_ref,
    const void* blob, size_t blob_size) {
    const size_t name_string_size = trace::SizeOfEncodedStringRef(name_ref);
    const size_t record_size_less_blob = sizeof(trace::RecordHeader) +
                                         name_string_size;
    const size_t padded_blob_size = trace::Pad(blob_size);
    const size_t max_record_size = trace::RecordFields::kMaxRecordSizeBytes;
    if (record_size_less_blob > max_record_size ||
            padded_blob_size > max_record_size - record_size_less_blob) {
        return;
    }
    const size_t record_size = record_size_less_blob + padded_blob_size;
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kBlob, record_size) |
                         trace::BlobRecordFields::BlobType::Make(
                             trace::ToUnderlyingType(type)) |
                         trace::BlobRecordFields::NameStringRef::Make(
                             name_ref->encoded_value) |
                         trace::BlobRecordFields::BlobSize::Make(blob_size))
            .WriteStringRef(name_ref)
            .WriteBytes(blob, blob_size);
    }
}

void trace_context_write_kernel_object_record(
    trace_context_t* context,
    bool use_durable,
    zx_koid_t koid, zx_obj_type_t type,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::WordsToBytes(1) +
                               trace::SizeOfEncodedStringRef(name_ref) +
                               trace::SizeOfEncodedArgs(args, num_args);
    trace::Payload payload(context, use_durable, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kKernelObject, record_size) |
                         trace::KernelObjectRecordFields::ObjectType::Make(type) |
                         trace::KernelObjectRecordFields::NameStringRef::Make(
                             name_ref->encoded_value) |
                         trace::KernelObjectRecordFields::ArgumentCount::Make(num_args))
            .WriteUint64(koid)
            .WriteStringRef(name_ref)
            .WriteArgs(args, num_args);
    }
}

void trace_context_write_kernel_object_record_for_handle(
    trace_context_t* context,
    zx_handle_t handle,
    const trace_arg_t* args, size_t num_args) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    if (status != ZX_OK)
        return;

    trace_string_ref name_ref;
    char name_buf[ZX_MAX_NAME_LEN];
    trace::GetObjectName(handle, name_buf, sizeof(name_buf), &name_ref);

    zx_obj_type_t obj_type = static_cast<zx_obj_type_t>(info.type);
    switch (obj_type) {
    case ZX_OBJ_TYPE_PROCESS:
        // TODO(ZX-1028): Support custom args.
        trace_context_write_process_info_record(context, info.koid, &name_ref);
        break;
    case ZX_OBJ_TYPE_THREAD:
        // TODO(ZX-1028): Support custom args.
        trace_context_write_thread_info_record(context, info.related_koid, info.koid, &name_ref);
        break;
    default:
        trace_context_write_kernel_object_record(context, false, info.koid,
                                                 obj_type, &name_ref,
                                                 args, num_args);
        break;
    }
}

void trace_context_write_process_info_record(
    trace_context_t* context,
    zx_koid_t process_koid,
    const trace_string_ref_t* process_name_ref) {
    trace_context_write_kernel_object_record(context, true,
                                             process_koid, ZX_OBJ_TYPE_PROCESS,
                                             process_name_ref, nullptr, 0u);
}

void trace_context_write_thread_info_record(
    trace_context_t* context,
    zx_koid_t process_koid,
    zx_koid_t thread_koid,
    const trace_string_ref_t* thread_name_ref) {
    // TODO(ZX-1028): We should probably store the related koid in the trace
    // event directly instead of packing it into an argument like this.
    trace_arg_t arg;
    trace_context_register_string_literal(context, "process", &arg.name_ref);
    arg.value.type = TRACE_ARG_KOID;
    arg.value.koid_value = process_koid;
    trace_context_write_kernel_object_record(context, true,
                                             thread_koid, ZX_OBJ_TYPE_THREAD,
                                             thread_name_ref, &arg, 1u);
}

void trace_context_write_context_switch_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    trace_cpu_number_t cpu_number,
    trace_thread_state_t outgoing_thread_state,
    const trace_thread_ref_t* outgoing_thread_ref,
    const trace_thread_ref_t* incoming_thread_ref,
    trace_thread_priority_t outgoing_thread_priority,
    trace_thread_priority_t incoming_thread_priority) {
    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::WordsToBytes(1) +
                               trace::SizeOfEncodedThreadRef(outgoing_thread_ref) +
                               trace::SizeOfEncodedThreadRef(incoming_thread_ref);
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kContextSwitch, record_size) |
                         trace::ContextSwitchRecordFields::CpuNumber::Make(cpu_number) |
                         trace::ContextSwitchRecordFields::OutgoingThreadState::Make(
                             ZX_THREAD_STATE_BASIC(outgoing_thread_state)) |
                         trace::ContextSwitchRecordFields::OutgoingThreadRef::Make(
                             outgoing_thread_ref->encoded_value) |
                         trace::ContextSwitchRecordFields::IncomingThreadRef::Make(
                             incoming_thread_ref->encoded_value) |
                         trace::ContextSwitchRecordFields::OutgoingThreadPriority::Make(
                             outgoing_thread_priority) |
                         trace::ContextSwitchRecordFields::IncomingThreadPriority::Make(
                             incoming_thread_priority))
            .WriteUint64(event_time)
            .WriteThreadRef(outgoing_thread_ref)
            .WriteThreadRef(incoming_thread_ref);
    }
}

void trace_context_write_log_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const char* log_message,
    size_t log_message_length) {
    if (!log_message)
        return;

    log_message_length =
        fbl::min(log_message_length, size_t(trace::LogRecordFields::kMaxMessageLength));
    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::SizeOfEncodedThreadRef(thread_ref) +
                               trace::WordsToBytes(1) +
                               trace::Pad(log_message_length);
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kLog, record_size) |
                         trace::LogRecordFields::LogMessageLength::Make(log_message_length) |
                         trace::LogRecordFields::ThreadRef::Make(thread_ref->encoded_value))
            .WriteUint64(event_time)
            .WriteThreadRef(thread_ref)
            .WriteBytes(log_message, log_message_length);
    }
}

void trace_context_write_instant_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_scope_t scope,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kInstant, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(trace::ToUnderlyingType(scope));
    }
}

void trace_context_write_counter_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_counter_id_t counter_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kCounter, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(counter_id);
    }
}

void trace_context_write_duration_event_record(
    trace_context_t* context,
    trace_ticks_t start_time,
    trace_ticks_t end_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
    trace_context_write_duration_begin_event_record(
        context, start_time,
        thread_ref, category_ref, name_ref,
        args, num_args);
    trace_context_write_duration_end_event_record(
        context, end_time,
        thread_ref, category_ref, name_ref,
        nullptr, 0u);
}

void trace_context_write_duration_begin_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
    trace::WriteEventRecordBase(
        context, trace::EventType::kDurationBegin, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, 0u);
}

void trace_context_write_duration_end_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
    trace::WriteEventRecordBase(
        context, trace::EventType::kDurationEnd, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, 0u);
}

void trace_context_write_async_begin_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kAsyncBegin, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(async_id);
    }
}

void trace_context_write_async_instant_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kAsyncInstant, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(async_id);
    }
}

void trace_context_write_async_end_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kAsyncEnd, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(async_id);
    }
}

void trace_context_write_flow_begin_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kFlowBegin, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(flow_id);
    }
}

void trace_context_write_flow_step_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kFlowStep, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(flow_id);
    }
}

void trace_context_write_flow_end_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args) {
    const size_t content_size = trace::WordsToBytes(1);
    trace::Payload payload = trace::WriteEventRecordBase(
        context, trace::EventType::kFlowEnd, event_time,
        thread_ref, category_ref, name_ref,
        args, num_args, content_size);
    if (payload) {
        payload.WriteUint64(flow_id);
    }
}

// TODO(dje): Move data to header?
void trace_context_write_initialization_record(
    trace_context_t* context,
    zx_ticks_t ticks_per_second) {
    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::WordsToBytes(1);
    trace::Payload payload(context, true, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kInitialization, record_size))
            .WriteUint64(ticks_per_second);
    }
}

void trace_context_write_string_record(
    trace_context_t* context,
    trace_string_index_t index, const char* string, size_t length) {
    if (unlikely(!trace::WriteStringRecord(context, false, index,
                                           string, length))) {
        // The write will fail if the buffer is full. Nothing we can do.
    }
}

void trace_context_write_thread_record(
    trace_context_t* context,
    trace_thread_index_t index,
    zx_koid_t process_koid,
    zx_koid_t thread_koid) {
    if (unlikely(!trace::WriteThreadRecord(context, index,
                                           process_koid, thread_koid))) {
        // The write will fail if the buffer is full. Nothing we can do.
    }
}

void* trace_context_alloc_record(trace_context_t* context, size_t num_bytes) {
    return context->AllocRecord(num_bytes);
}

void trace_context_snapshot_buffer_header(
    trace_prolonged_context_t* context,
    ::trace::internal::trace_buffer_header* header) {
    auto ctx = reinterpret_cast<trace_context_t*>(context);
    ctx->UpdateBufferHeaderAfterStopped();
    memcpy(header, ctx->buffer_header(), sizeof(*header));
}

/* struct trace_context */

trace_context::trace_context(void* buffer, size_t buffer_num_bytes,
                             trace_buffering_mode_t buffering_mode,
                             trace_handler_t* handler)
    : generation_(trace::g_next_generation.fetch_add(1u, fbl::memory_order_relaxed) + 1u),
      buffering_mode_(buffering_mode),
      buffer_start_(reinterpret_cast<uint8_t*>(buffer)),
      buffer_end_(buffer_start_ + buffer_num_bytes),
      header_(reinterpret_cast<trace_buffer_header*>(buffer)),
      handler_(handler) {
    ZX_DEBUG_ASSERT(buffer_num_bytes >= kMinPhysicalBufferSize);
    ZX_DEBUG_ASSERT(buffer_num_bytes <= kMaxPhysicalBufferSize);
    ZX_DEBUG_ASSERT(generation_ != 0u);
    ComputeBufferSizes();
}

trace_context::~trace_context() = default;

uint64_t* trace_context::AllocRecord(size_t num_bytes) {
    ZX_DEBUG_ASSERT((num_bytes & 7) == 0);
    if (unlikely(num_bytes > TRACE_ENCODED_RECORD_MAX_LENGTH))
        return nullptr;
    static_assert(TRACE_ENCODED_RECORD_MAX_LENGTH < kMaxRollingBufferSize, "");

    // For the circular and streaming cases, try at most once for each buffer.
    // Note: Keep the normal case of one successful pass the fast path.
    // E.g., We don't do a mode comparison unless we have to.

    for (int iter = 0; iter < 2; ++iter) {
        // TODO(dje): This can be optimized a bit. Later.
        uint64_t offset_plus_counter =
            rolling_buffer_current_.fetch_add(num_bytes,
                                              fbl::memory_order_relaxed);
        uint32_t wrapped_count = GetWrappedCount(offset_plus_counter);
        int buffer_number = GetBufferNumber(wrapped_count);
        uint64_t buffer_offset  = GetBufferOffset(offset_plus_counter);
        // Note: There's no worry of an overflow in the calcs here.
        if (likely(buffer_offset + num_bytes <= rolling_buffer_size_)) {
            uint8_t* ptr = rolling_buffer_start_[buffer_number] + buffer_offset;
            return reinterpret_cast<uint64_t*>(ptr); // success!
        }

        // Buffer is full!

        switch (buffering_mode_) {
        case TRACE_BUFFERING_MODE_ONESHOT:
            ZX_DEBUG_ASSERT(iter == 0);
            ZX_DEBUG_ASSERT(wrapped_count == 0);
            ZX_DEBUG_ASSERT(buffer_number == 0);
            MarkOneshotBufferFull(buffer_offset);
            return nullptr;
        case TRACE_BUFFERING_MODE_STREAMING: {
            MarkRollingBufferFull(wrapped_count, buffer_offset);
            // If the TraceManager is slow in saving buffers we could get
            // here a lot. Do a quick check and early exit for this case.
            if (unlikely(!IsOtherRollingBufferReady(buffer_number))) {
                MarkRecordDropped();
                StreamingBufferFullCheck(wrapped_count, buffer_offset);
                return nullptr;
            }
            break;
        }
        case TRACE_BUFFERING_MODE_CIRCULAR:
            MarkRollingBufferFull(wrapped_count, buffer_offset);
            break;
        default:
            __UNREACHABLE;
        }

        if (iter == 1) {
            // Second time through. We tried one buffer, it was full.
            // We then switched to the other buffer, which was empty at
            // the time, and now it is full too. This is technically
            // possible in either circular or streaming modes, but rare.
            // There are two possibilities here:
            // 1) Keep trying (gated by some means).
            // 2) Drop the record.
            // In order to not introduce excessive latency into the app
            // we choose (2). To assist the developer we at least provide
            // a record that this happened, but since it's rare we keep
            // it simple and maintain just a global count and no time
            // information.
            num_records_dropped_after_buffer_switch_.fetch_add(1, fbl::memory_order_relaxed);
            return nullptr;
        }

        if (!SwitchRollingBuffer(wrapped_count, buffer_offset)) {
            MarkRecordDropped();
            return nullptr;
        }

        // Loop and try again.
    }

    __UNREACHABLE;
}

void trace_context::StreamingBufferFullCheck(uint32_t wrapped_count,
                                             uint64_t buffer_offset) {
    // We allow the current offset to grow and grow as each
    // new tracing request is made: It's a trade-off to not penalize
    // performance in this case. The number of counter bits is enough
    // to not make this a concern: See the comments for
    // |kUsableBufferOffsetBits|.
    //
    // As an absolute paranoia check, if the current buffer offset
    // approaches overflow, grab the lock and snap the offset back
    // to the end of the buffer. We grab the lock so that the
    // buffer can't change while we're doing this.
    if (unlikely(buffer_offset > MaxUsableBufferOffset())) {
        fbl::AutoLock lock(&buffer_switch_mutex_);
        uint32_t current_wrapped_count = CurrentWrappedCount();
        if (GetBufferNumber(current_wrapped_count) ==
            GetBufferNumber(wrapped_count)) {
            SnapToEnd(wrapped_count);
        }
    }
}

// Returns false if there's some reason to not record this record.

bool trace_context::SwitchRollingBuffer(uint32_t wrapped_count,
                                        uint64_t buffer_offset) {
    // While atomic variables are used to track things, we switch
    // buffers under the lock due to multiple pieces of state being
    // changed.
    fbl::AutoLock lock(&buffer_switch_mutex_);

    // If the durable buffer happened to fill while we were waiting for
    // the lock we're done.
    if (unlikely(tracing_artificially_stopped_)) {
        return false;
    }

    uint32_t current_wrapped_count = CurrentWrappedCount();
    // Anything allocated to the durable buffer after this point
    // won't be for this buffer. This is racy, but all we need is
    // some usable value for where the durable pointer is.
    uint64_t durable_data_end = DurableBytesAllocated();

    ZX_DEBUG_ASSERT(wrapped_count <= current_wrapped_count);
    if (likely(wrapped_count == current_wrapped_count)) {
        // Haven't switched buffers yet.
        if (buffering_mode_ == TRACE_BUFFERING_MODE_STREAMING) {
            // Is the other buffer ready?
            if (!IsOtherRollingBufferReady(GetBufferNumber(wrapped_count))) {
                // Nope. There are two possibilities here:
                // 1) Wait for the other buffer to be saved.
                // 2) Start dropping records until the other buffer is
                //    saved.
                // In order to not introduce excessive latency into the
                // app we choose (2). To assist the developer we at
                // least provide a record that indicates the window
                // during which we dropped records.
                // TODO(dje): Maybe have a future mode where we block
                // until there's space. This is useful during some
                // kinds of debugging: Something is going wrong and we
                // care less about performance and more about keeping
                // data, and the dropped data may be the clue to find
                // the bug.
                return false;
            }

            SwitchRollingBufferLocked(wrapped_count, buffer_offset);

            // Notify the handler so it starts saving the buffer if
            // we're in streaming mode.
            // Note: The actual notification must be done *after*
            // updating the buffer header: we need trace_manager to
            // see the updates. The handler will get notified on the
            // engine's async loop (and thus can't call back into us
            // while we still have the lock).
            NotifyRollingBufferFullLocked(wrapped_count, durable_data_end);
        } else {
            SwitchRollingBufferLocked(wrapped_count, buffer_offset);
        }
    } else {
        // Someone else switched buffers while we were trying to obtain
        // the lock. Nothing to do here.
    }

    return true;
}

uint64_t* trace_context::AllocDurableRecord(size_t num_bytes) {
    ZX_DEBUG_ASSERT(UsingDurableBuffer());
    ZX_DEBUG_ASSERT((num_bytes & 7) == 0);

    uint64_t buffer_offset =
        durable_buffer_current_.fetch_add(num_bytes,
                                          fbl::memory_order_relaxed);
    if (likely(buffer_offset + num_bytes <= durable_buffer_size_)) {
        uint8_t* ptr = durable_buffer_start_ + buffer_offset;
        return reinterpret_cast<uint64_t*>(ptr); // success!
    }

    // Buffer is full!
    MarkDurableBufferFull(buffer_offset);

    return nullptr;
}

bool trace_context::AllocThreadIndex(trace_thread_index_t* out_index) {
    trace_thread_index_t index = next_thread_index_.fetch_add(1u, fbl::memory_order_relaxed);
    if (unlikely(index > TRACE_ENCODED_THREAD_REF_MAX_INDEX)) {
        // Guard again possible wrapping.
        next_thread_index_.store(TRACE_ENCODED_THREAD_REF_MAX_INDEX + 1u,
                                 fbl::memory_order_relaxed);
        return false;
    }
    *out_index = index;
    return true;
}

bool trace_context::AllocStringIndex(trace_string_index_t* out_index) {
    trace_string_index_t index = next_string_index_.fetch_add(1u, fbl::memory_order_relaxed);
    if (unlikely(index > TRACE_ENCODED_STRING_REF_MAX_INDEX)) {
        // Guard again possible wrapping.
        next_string_index_.store(TRACE_ENCODED_STRING_REF_MAX_INDEX + 1u,
                                 fbl::memory_order_relaxed);
        return false;
    }
    *out_index = index;
    return true;
}

void trace_context::ComputeBufferSizes() {
    size_t full_buffer_size = buffer_end_ - buffer_start_;
    ZX_DEBUG_ASSERT(full_buffer_size >= kMinPhysicalBufferSize);
    ZX_DEBUG_ASSERT(full_buffer_size <= kMaxPhysicalBufferSize);
    size_t header_size = sizeof(trace_buffer_header);
    switch (buffering_mode_) {
    case TRACE_BUFFERING_MODE_ONESHOT:
        // Create one big buffer, where durable and non-durable records share
        // the same buffer. There is no separate buffer for durable records.
        durable_buffer_start_ = nullptr;
        durable_buffer_size_ = 0;
        rolling_buffer_start_[0] = buffer_start_ + header_size;
        rolling_buffer_size_ = full_buffer_size - header_size;
        // The second rolling buffer is not used.
        rolling_buffer_start_[1] = nullptr;
        break;
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING: {
        // Rather than make things more complex on the user, at least for now,
        // we choose the sizes of the durable and rolling buffers.
        // Note: The durable buffer must have enough space for at least
        // the initialization record.
        // TODO(dje): The current choices are wip.
        uint64_t avail = full_buffer_size - header_size;
        uint64_t durable_buffer_size = GET_DURABLE_BUFFER_SIZE(avail);
        if (durable_buffer_size > kMaxDurableBufferSize)
            durable_buffer_size = kMaxDurableBufferSize;
        // Further adjust |durable_buffer_size| to ensure all buffers are a
        // multiple of 8. |full_buffer_size| is guaranteed by
        // |trace_start_engine()| to be a multiple of 4096. We only assume
        // header_size is a multiple of 8. In order for rolling_buffer_size
        // to be a multiple of 8 we need (avail - durable_buffer_size) to be a
        // multiple of 16. Round durable_buffer_size up as necessary.
        uint64_t off_by = (avail - durable_buffer_size) & 15;
        ZX_DEBUG_ASSERT(off_by == 0 || off_by == 8);
        durable_buffer_size += off_by;
        ZX_DEBUG_ASSERT((durable_buffer_size & 7) == 0);
        // The value of |kMinPhysicalBufferSize| ensures this:
        ZX_DEBUG_ASSERT(durable_buffer_size >= kMinDurableBufferSize);
        uint64_t rolling_buffer_size = (avail - durable_buffer_size) / 2;
        ZX_DEBUG_ASSERT((rolling_buffer_size & 7) == 0);
        // We need to maintain the invariant that the entire buffer is used.
        // This works if the buffer size is a multiple of
        // sizeof(trace_buffer_header), which is true since the buffer is a
        // vmo (some number of 4K pages).
        ZX_DEBUG_ASSERT(durable_buffer_size + 2 * rolling_buffer_size == avail);
        durable_buffer_start_ = buffer_start_ + header_size;
        durable_buffer_size_ = durable_buffer_size;
        rolling_buffer_start_[0] = durable_buffer_start_ + durable_buffer_size_;
        rolling_buffer_start_[1] = rolling_buffer_start_[0] + rolling_buffer_size;
        rolling_buffer_size_ = rolling_buffer_size;
        break;
    }
    default:
        __UNREACHABLE;
    }

    durable_buffer_current_.store(0);
    durable_buffer_full_mark_.store(0);
    rolling_buffer_current_.store(0);
    rolling_buffer_full_mark_[0].store(0);
    rolling_buffer_full_mark_[1].store(0);
}

void trace_context::InitBufferHeader() {
    memset(header_, 0, sizeof(*header_));

    header_->magic = TRACE_BUFFER_HEADER_MAGIC;
    header_->version = TRACE_BUFFER_HEADER_V0;
    header_->buffering_mode = static_cast<uint8_t>(buffering_mode_);
    header_->total_size = buffer_end_ - buffer_start_;
    header_->durable_buffer_size = durable_buffer_size_;
    header_->rolling_buffer_size = rolling_buffer_size_;
}

void trace_context::UpdateBufferHeaderAfterStopped() {
    // If the buffer filled, then the current pointer is "snapped" to the end.
    // Therefore in that case we need to use the buffer_full_mark.
    uint64_t durable_last_offset = durable_buffer_current_.load(fbl::memory_order_relaxed);
    uint64_t durable_buffer_full_mark = durable_buffer_full_mark_.load(fbl::memory_order_relaxed);
    if (durable_buffer_full_mark != 0)
        durable_last_offset = durable_buffer_full_mark;
    header_->durable_data_end = durable_last_offset;

    uint64_t offset_plus_counter =
        rolling_buffer_current_.load(fbl::memory_order_relaxed);
    uint64_t last_offset = GetBufferOffset(offset_plus_counter);
    uint32_t wrapped_count = GetWrappedCount(offset_plus_counter);
    header_->wrapped_count = wrapped_count;
    int buffer_number = GetBufferNumber(wrapped_count);
    uint64_t buffer_full_mark = rolling_buffer_full_mark_[buffer_number].load(fbl::memory_order_relaxed);
    if (buffer_full_mark != 0)
        last_offset = buffer_full_mark;
    header_->rolling_data_end[buffer_number] = last_offset;

    header_->num_records_dropped = num_records_dropped();
}

size_t trace_context::RollingBytesAllocated() const {
    switch (buffering_mode_) {
    case TRACE_BUFFERING_MODE_ONESHOT: {
        // There is a window during the processing of buffer-full where
        // |rolling_buffer_current_| may point beyond the end of the buffer.
        // This is ok, we don't promise anything better.
        uint64_t full_bytes = rolling_buffer_full_mark_[0].load(fbl::memory_order_relaxed);
        if (full_bytes != 0)
            return full_bytes;
        return rolling_buffer_current_.load(fbl::memory_order_relaxed);
    }
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING: {
        // Obtain the lock so that the buffers aren't switched on us while
        // we're trying to compute the total.
        fbl::AutoLock lock(&buffer_switch_mutex_);
        uint64_t offset_plus_counter =
            rolling_buffer_current_.load(fbl::memory_order_relaxed);
        uint32_t wrapped_count = GetWrappedCount(offset_plus_counter);
        int buffer_number = GetBufferNumber(wrapped_count);
        // Note: If we catch things at the point where the buffer has
        // filled, but before we swap buffers, then |buffer_offset| can point
        // beyond the end. This is ok, we don't promise anything better.
        uint64_t buffer_offset  = GetBufferOffset(offset_plus_counter);
        if (wrapped_count == 0)
            return buffer_offset;
        // We've wrapped at least once, so the other buffer's "full mark"
        // must be set. However, it may be zero if streaming and we happened
        // to stop at a point where the buffer was saved, and hasn't
        // subsequently been written to.
        uint64_t full_mark_other_buffer = rolling_buffer_full_mark_[!buffer_number].load(fbl::memory_order_relaxed);
        return full_mark_other_buffer + buffer_offset;
    }
    default:
        __UNREACHABLE;
    }
}

size_t trace_context::DurableBytesAllocated() const {
    // Note: This will return zero in oneshot mode (as it should).
    uint64_t offset = durable_buffer_full_mark_.load(fbl::memory_order_relaxed);
    if (offset == 0)
        offset = durable_buffer_current_.load(fbl::memory_order_relaxed);
    return offset;
}

void trace_context::MarkDurableBufferFull(uint64_t last_offset) {
    // Snap to the endpoint to reduce likelihood of pointer wrap-around.
    // Otherwise each new attempt fill continually increase the offset.
    durable_buffer_current_.store(reinterpret_cast<uint64_t>(durable_buffer_size_),
                                  fbl::memory_order_relaxed);

    // Mark the end point if not already marked.
    uintptr_t expected_mark = 0u;
    if (durable_buffer_full_mark_.compare_exchange_strong(
            &expected_mark, last_offset,
            fbl::memory_order_relaxed, fbl::memory_order_relaxed)) {
        printf("TraceEngine: durable buffer full @offset %" PRIu64 "\n",
               last_offset);
        header_->durable_data_end = last_offset;

        // A record may be written that relies on this durable record.
        // To preserve data integrity, we disable all further tracing.
        // There is a small window where a non-durable record could get
        // emitted that depends on this durable record. It's rare
        // enough and inconsequential enough that we ignore it.
        // TODO(dje): Another possibility is we could let tracing
        // continue and start allocating future durable records in the
        // rolling buffers, and accept potentially lost durable
        // records. Another possibility is to remove the durable buffer,
        // and, say, have separate caches for each rolling buffer.
        MarkTracingArtificiallyStopped();
    }
}

void trace_context::MarkOneshotBufferFull(uint64_t last_offset) {
    SnapToEnd(0);

    // Mark the end point if not already marked.
    uintptr_t expected_mark = 0u;
    if (rolling_buffer_full_mark_[0].compare_exchange_strong(
            &expected_mark, last_offset,
            fbl::memory_order_relaxed, fbl::memory_order_relaxed)) {
        header_->rolling_data_end[0] = last_offset;
    }

    MarkRecordDropped();
}

void trace_context::MarkRollingBufferFull(uint32_t wrapped_count, uint64_t last_offset) {
    // Mark the end point if not already marked.
    int buffer_number = GetBufferNumber(wrapped_count);
    uint64_t expected_mark = 0u;
    if (rolling_buffer_full_mark_[buffer_number].compare_exchange_strong(
            &expected_mark, last_offset,
            fbl::memory_order_relaxed, fbl::memory_order_relaxed)) {
        header_->rolling_data_end[buffer_number] = last_offset;
    }
}

void trace_context::SwitchRollingBufferLocked(uint32_t prev_wrapped_count,
                                              uint64_t prev_last_offset) {
    // This has already done in streaming mode when the buffer was marked as
    // saved, but hasn't been done yet for circular mode. KISS and just do it
    // again. It's ok to do again as we don't resume allocating trace records
    // until we update |rolling_buffer_current_|.
    uint32_t new_wrapped_count = prev_wrapped_count + 1;
    int next_buffer = GetBufferNumber(new_wrapped_count);
    rolling_buffer_full_mark_[next_buffer].store(0, fbl::memory_order_relaxed);
    header_->rolling_data_end[next_buffer] = 0;

    // Do this last: After this tracing resumes in the new buffer.
    uint64_t new_offset_plus_counter = MakeOffsetPlusCounter(0, new_wrapped_count);
    rolling_buffer_current_.store(new_offset_plus_counter,
                                     fbl::memory_order_relaxed);
}

void trace_context::MarkTracingArtificiallyStopped() {
    // Grab the lock in part so that we don't switch buffers between
    // |CurrentWrappedCount()| and |SnapToEnd()|.
    fbl::AutoLock lock(&buffer_switch_mutex_);

    // Disable tracing by making it look like the current rolling
    // buffer is full. AllocRecord, on seeing the buffer is full, will
    // then check |tracing_artificially_stopped_|.
    tracing_artificially_stopped_ = true;
    SnapToEnd(CurrentWrappedCount());
}

void trace_context::NotifyRollingBufferFullLocked(uint32_t wrapped_count,
                                                  uint64_t durable_data_end) {
    // The notification is handled on the engine's event loop as
    // we need this done outside of the lock: Certain handlers
    // (e.g., trace-benchmark) just want to immediately call
    // |trace_engine_mark_buffer_saved()| which wants to reacquire
    // the lock. Secondly, if we choose to wait until the buffer context is
    // released before notifying the handler then we can't do so now as we
    // still have a reference to the buffer context.
    trace_engine_request_save_buffer(wrapped_count, durable_data_end);
}

void trace_context::HandleSaveRollingBufferRequest(uint32_t wrapped_count,
                                                   uint64_t durable_data_end) {
    // TODO(dje): An open issue is solving the problem of TraceManager
    // prematurely reading the buffer: We know the buffer is full, but
    // the only way we know existing writers have completed is when
    // they release their trace context. Fortunately we know when all
    // context acquisitions for the purpose of writing to the buffer
    // have been released. The question is how to use this info.
    // For now we punt the problem to the handler. Ultimately we could
    // provide callers with a way to wait, and have trace_release_context()
    // check for waiters and if any are present send a signal like it does
    // for SIGNAL_CONTEXT_RELEASED.
    handler_->ops->notify_buffer_full(handler_, wrapped_count,
                                      durable_data_end);
}

void trace_context::MarkRollingBufferSaved(uint32_t wrapped_count,
                                           uint64_t durable_data_end) {
    fbl::AutoLock lock(&buffer_switch_mutex_);

    int buffer_number = GetBufferNumber(wrapped_count);
    {
        // TODO(dje): Manage bad responses from TraceManager.
        int current_buffer_number = GetBufferNumber(GetWrappedCount(rolling_buffer_current_.load(fbl::memory_order_relaxed)));
        ZX_DEBUG_ASSERT(buffer_number != current_buffer_number);
    }
    rolling_buffer_full_mark_[buffer_number].store(0, fbl::memory_order_relaxed);
    header_->rolling_data_end[buffer_number] = 0;
    // Don't update |rolling_buffer_current_| here, that is done when we
    // successfully allocate the next record. Until then we want to keep
    // dropping records.
}

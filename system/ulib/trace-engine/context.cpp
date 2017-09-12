// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "context_impl.h"

#include <magenta/compiler.h>
#include <magenta/syscalls.h>

#include <fbl/algorithm.h>
#include <fbl/atomic.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <mx/process.h>
#include <mx/thread.h>
#include <trace-engine/fields.h>

namespace trace {
namespace {

// The cached koid of this process.
// Initialized on first use.
fbl::atomic<uint64_t> g_process_koid{MX_KOID_INVALID};

// This thread's koid.
// Initialized on first use.
thread_local mx_koid_t tls_thread_koid{MX_KOID_INVALID};

mx_koid_t GetKoid(mx_handle_t handle) {
    mx_info_handle_basic_t info;
    mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    return status == MX_OK ? info.koid : MX_KOID_INVALID;
}

mx_koid_t GetCurrentProcessKoid() {
    mx_koid_t koid = g_process_koid.load(fbl::memory_order_relaxed);
    if (unlikely(koid == MX_KOID_INVALID)) {
        koid = GetKoid(mx::process::self().get());
        g_process_koid.store(koid, fbl::memory_order_relaxed); // idempotent
    }
    return koid;
}

mx_koid_t GetCurrentThreadKoid() {
    if (unlikely(tls_thread_koid == MX_KOID_INVALID)) {
        tls_thread_koid = GetKoid(mx::thread::self().get());
    }
    return tls_thread_koid;
}

void GetObjectName(mx_handle_t handle, char* name_buf, size_t name_buf_size,
                   trace_string_ref* out_name_ref) {
    mx_status_t status = mx_object_get_property(handle, MX_PROP_NAME,
                                                name_buf, name_buf_size);
    name_buf[name_buf_size - 1] = 0;
    if (status == MX_OK) {
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
    // TODO(MG-1030): Unknown thread refs should not be stored inline.
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
        MX_DEBUG_ASSERT(false);
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

    Payload& WriteBytes(const void* src, size_t length) {
        memcpy(ptr_, src, length);
        ptr_ += length / 8u;
        size_t tail = length & 7u;
        if (tail) {
            size_t padding = 8u - tail;
            ptr_++;
            memset(reinterpret_cast<uint8_t*>(ptr_) - padding, 0u, padding);
        }
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
        // TODO(MG-1030): Unknown thread refs should not be stored inline.
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
            MX_DEBUG_ASSERT(false);
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
                if (context->AllocStringIndex(&entry->index)) {
                    entry->flags |= StringEntry::kAllocIndexAttempted |
                                    StringEntry::kAllocIndexSucceeded;
                    trace_context_write_string_record(context, entry->index,
                                                      string_literal, strlen(string_literal));
                } else {
                    entry->flags |= StringEntry::kAllocIndexAttempted;
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
    // TODO(MG-1035): Since we can't use the thread-local cache here, cache
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
    // TODO(MG-1035): Cache the registered strings on the trace context structure,
    // guarded by a mutex.
    trace_string_index_t index;
    if (likely(context->AllocStringIndex(&index))) {
        trace_context_write_string_record(context, index, string, length);
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
    MX_DEBUG_ASSERT(result);
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
    char name_buf[MX_MAX_NAME_LEN];
    trace::GetObjectName(mx::thread::self().get(), name_buf, sizeof(name_buf), &name_ref);
    mx_koid_t process_koid = trace::GetCurrentProcessKoid();
    mx_koid_t thread_koid = trace::GetCurrentThreadKoid();
    trace_context_write_thread_info_record(context, process_koid, thread_koid,
                                           &name_ref);

    if (likely(cache)) {
        trace_thread_index_t index;
        if (likely(context->AllocThreadIndex(&index))) {
            cache->thread_ref = trace_make_indexed_thread_ref(index);
            trace_context_write_thread_record(context, index, process_koid, thread_koid);
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
    mx_koid_t process_koid, mx_koid_t thread_koid,
    trace_thread_ref_t* out_ref) {
    // TODO(MG-1035): Since we can't use the thread-local cache here, cache
    // this registered thread on the trace context structure, guarded by a mutex.
    trace_thread_index_t index;
    if (likely(context->AllocThreadIndex(&index))) {
        trace_context_write_thread_record(context, index, process_koid, thread_koid);
        *out_ref = trace_make_indexed_thread_ref(index);
    } else {
        *out_ref = trace_make_inline_thread_ref(process_koid, thread_koid);
    }
}

void trace_context_write_kernel_object_record(
    trace_context_t* context,
    mx_koid_t koid, mx_obj_type_t type,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::WordsToBytes(1) +
                               trace::SizeOfEncodedStringRef(name_ref) +
                               trace::SizeOfEncodedArgs(args, num_args);
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kKernelObject, record_size) |
                         trace::KernelObjectRecordFields::ObjectType::Make(
                             trace::ToUnderlyingType(type)) |
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
    mx_handle_t handle,
    const trace_arg_t* args, size_t num_args) {
    mx_info_handle_basic_t info;
    mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    if (status != MX_OK)
        return;

    trace_string_ref name_ref;
    char name_buf[MX_MAX_NAME_LEN];
    trace::GetObjectName(handle, name_buf, sizeof(name_buf), &name_ref);

    mx_obj_type_t obj_type = static_cast<mx_obj_type_t>(info.type);
    switch (obj_type) {
    case MX_OBJ_TYPE_PROCESS:
        // TODO(MG-1028): Support custom args.
        trace_context_write_process_info_record(context, info.koid, &name_ref);
        break;
    case MX_OBJ_TYPE_THREAD:
        // TODO(MG-1028): Support custom args.
        trace_context_write_thread_info_record(context, info.related_koid, info.koid, &name_ref);
        break;
    default:
        trace_context_write_kernel_object_record(context, info.koid, obj_type, &name_ref,
                                                 args, num_args);
        break;
    }
}

void trace_context_write_process_info_record(
    trace_context_t* context,
    mx_koid_t process_koid,
    const trace_string_ref_t* process_name_ref) {
    trace_context_write_kernel_object_record(context, process_koid, MX_OBJ_TYPE_PROCESS,
                                             process_name_ref, nullptr, 0u);
}

void trace_context_write_thread_info_record(
    trace_context_t* context,
    mx_koid_t process_koid,
    mx_koid_t thread_koid,
    const trace_string_ref_t* thread_name_ref) {
    // TODO(MG-1028): We should probably store the related koid in the trace
    // event directly instead of packing it into an argument like this.
    trace_arg_t arg;
    trace_context_register_string_literal(context, "process", &arg.name_ref);
    arg.value.type = TRACE_ARG_KOID;
    arg.value.koid_value = process_koid;
    trace_context_write_kernel_object_record(context, process_koid, MX_OBJ_TYPE_THREAD,
                                             thread_name_ref, &arg, 1u);
}

void trace_context_write_context_switch_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    trace_cpu_number_t cpu_number,
    trace_thread_state_t outgoing_thread_state,
    const trace_thread_ref_t* outgoing_thread_ref,
    const trace_thread_ref_t* incoming_thread_ref) {
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
                             outgoing_thread_state) |
                         trace::ContextSwitchRecordFields::OutgoingThreadRef::Make(
                             outgoing_thread_ref->encoded_value) |
                         trace::ContextSwitchRecordFields::IncomingThreadRef::Make(
                             incoming_thread_ref->encoded_value))
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

void trace_context_write_initialization_record(
    trace_context_t* context,
    uint64_t ticks_per_second) {
    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::WordsToBytes(1);
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kInitialization, record_size))
            .WriteUint64(ticks_per_second);
    }
}

void trace_context_write_string_record(
    trace_context_t* context,
    trace_string_index_t index, const char* string, size_t length) {
    MX_DEBUG_ASSERT(index != TRACE_ENCODED_STRING_REF_EMPTY);
    MX_DEBUG_ASSERT(index <= TRACE_ENCODED_STRING_REF_MAX_INDEX);

    if (length > TRACE_ENCODED_STRING_REF_MAX_LENGTH)
        length = TRACE_ENCODED_STRING_REF_MAX_LENGTH;

    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::Pad(length);
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kString, record_size) |
                         trace::StringRecordFields::StringIndex::Make(index) |
                         trace::StringRecordFields::StringLength::Make(length))
            .WriteBytes(string, length);
    }
}

void trace_context_write_thread_record(
    trace_context_t* context,
    trace_thread_index_t index,
    mx_koid_t process_koid,
    mx_koid_t thread_koid) {
    MX_DEBUG_ASSERT(index != TRACE_ENCODED_THREAD_REF_INLINE);
    MX_DEBUG_ASSERT(index <= TRACE_ENCODED_THREAD_REF_MAX_INDEX);

    const size_t record_size = sizeof(trace::RecordHeader) +
                               trace::WordsToBytes(2);
    trace::Payload payload(context, record_size);
    if (payload) {
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kThread, record_size) |
                         trace::ThreadRecordFields::ThreadIndex::Make(index))
            .WriteUint64(process_koid)
            .WriteUint64(thread_koid);
    }
}

void* trace_context_alloc_record(trace_context_t* context, size_t num_bytes) {
    return context->AllocRecord(num_bytes);
}

/* struct trace_context */

trace_context::trace_context(void* buffer, size_t buffer_num_bytes,
                             trace_handler_t* handler)
    : generation_(trace::g_next_generation.fetch_add(1u, fbl::memory_order_relaxed) + 1u),
      buffer_start_(static_cast<uint8_t*>(buffer)),
      buffer_end_(buffer_start_ + buffer_num_bytes),
      buffer_current_(reinterpret_cast<uintptr_t>(buffer_start_)),
      buffer_full_mark_(0u),
      handler_(handler) {
    MX_DEBUG_ASSERT(generation_ != 0u);
}

trace_context::~trace_context() = default;

uint64_t* trace_context::AllocRecord(size_t num_bytes) {
    MX_DEBUG_ASSERT((num_bytes & 7) == 0);
    if (unlikely(num_bytes > TRACE_ENCODED_RECORD_MAX_LENGTH))
        return nullptr;

    uint8_t* ptr = reinterpret_cast<uint8_t*>(
        buffer_current_.fetch_add(num_bytes,
                                  fbl::memory_order_relaxed));
    if (likely(ptr + num_bytes <= buffer_end_)) {
        MX_DEBUG_ASSERT(ptr + num_bytes >= buffer_start_);
        return reinterpret_cast<uint64_t*>(ptr); // success!
    }

    // Buffer is full!
    // Snap to the endpoint to reduce likelihood of pointer wrap-around.
    buffer_current_.store(reinterpret_cast<uintptr_t>(buffer_end_),
                          fbl::memory_order_relaxed);

    // Mark the end point if not already marked.
    if (unlikely(ptr != buffer_end_)) {
        buffer_full_mark_.store(reinterpret_cast<uintptr_t>(ptr),
                                fbl::memory_order_relaxed);
    }
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

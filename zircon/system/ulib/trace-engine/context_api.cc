// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trace-engine/fields.h>
#include <lib/trace-engine/handler.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <atomic>
#include <memory>

#include "context_impl.h"
#include "hash_table.h"

namespace trace {
namespace {

// Zircon defines all koids with bit 63 set as being artificial.
constexpr uint64_t kArtificialKoidFlag = 1ul << 63;

zx_koid_t MakeArtificialKoid(trace_vthread_id_t id) { return id | kArtificialKoidFlag; }

// The cached koid of this process.
// Initialized on first use.
std::atomic<uint64_t> g_process_koid{ZX_KOID_INVALID};

// This thread's koid.
// Initialized on first use.
thread_local zx_koid_t tls_thread_koid{ZX_KOID_INVALID};

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetCurrentProcessKoid() {
  zx_koid_t koid = g_process_koid.load(std::memory_order_relaxed);
  if (unlikely(koid == ZX_KOID_INVALID)) {
    koid = GetKoid(zx_process_self());
    g_process_koid.store(koid, std::memory_order_relaxed);  // idempotent
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
  zx_status_t status = zx_object_get_property(handle, ZX_PROP_NAME, name_buf, name_buf_size);
  name_buf[name_buf_size - 1] = 0;
  if (status == ZX_OK) {
    *out_name_ref = trace_make_inline_c_string_ref(name_buf);
  } else {
    *out_name_ref = trace_make_empty_string_ref();
  }
}

// A string table entry.
struct StringEntry : public internal::SinglyLinkedListable<StringEntry> {
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

// A thread table entry.
struct ThreadEntry : public internal::SinglyLinkedListable<ThreadEntry> {
  // The thread koid itself.
  zx_koid_t thread_koid;

  // Thread reference for this thread.
  trace_thread_ref_t thread_ref{};

  // Used by the hash table.
  zx_koid_t GetKey() const { return thread_koid; }
  static size_t GetHash(zx_koid_t key) { return key; }
};

// Cached thread and string data for a context.
// Each thread has its own cache of context state to avoid locking overhead
// while writing trace events in the common case.  There may be some
// duplicate registration of strings across threads.
struct ContextCache {
  ContextCache() = default;
  ~ContextCache() {
    string_table.clear();
    thread_table.clear();
  }

  // The generation number of the context which last modified this state.
  uint32_t generation{0u};

  // Thread reference created when this thread was registered.
  trace_thread_ref_t thread_ref{};

  // Maximum number of strings to cache per thread.
  static constexpr size_t kMaxStringEntries = 256;

  // String table.
  // Provides a limited amount of storage for rapidly looking up string literals
  // registered by this thread.
  internal::HashTable<const char*, StringEntry> string_table;

  // Storage for the string entries.
  StringEntry string_entries[kMaxStringEntries];

  // Maximum number of external thread references to cache per thread.
  static constexpr size_t kMaxThreadEntries = 4;

  // External thread table.
  // Provides a limited amount of storage for rapidly looking up external threads
  // registered by this thread.
  internal::HashTable<zx_koid_t, ThreadEntry> thread_table;

  // Storage for the external thread entries.
  ThreadEntry thread_entries[kMaxThreadEntries];
};
thread_local std::unique_ptr<ContextCache> tls_cache{};

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
  cache->thread_table.clear();
  return cache;
}

StringEntry* CacheStringEntry(uint32_t generation, const char* string_literal) {
  ContextCache* cache = GetCurrentContextCache(generation);
  if (unlikely(!cache))
    return nullptr;

  auto ptr = cache->string_table.lookup(string_literal);
  if (likely(ptr != nullptr)) {
    return ptr;
  }

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

ThreadEntry* CacheThreadEntry(uint32_t generation, zx_koid_t thread_koid) {
  ContextCache* cache = GetCurrentContextCache(generation);
  if (unlikely(!cache))
    return nullptr;

  auto ptr = cache->thread_table.lookup(thread_koid);
  if (likely(ptr != nullptr)) {
    return ptr;
  }

  size_t count = cache->thread_table.size();
  if (unlikely(count == ContextCache::kMaxThreadEntries))
    return nullptr;

  ThreadEntry* entry = &cache->thread_entries[count];
  entry->thread_koid = thread_koid;
  entry->thread_ref = trace_make_unknown_thread_ref();
  cache->thread_table.insert(entry);
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
  return trace_is_inline_string_ref(string_ref) ? Pad(trace_inline_string_ref_length(string_ref))
                                                : 0u;
}

size_t SizeOfEncodedThreadRef(const trace_thread_ref_t* thread_ref) {
  // TODO(fxbug.dev/30974): Unknown thread refs should not be stored inline.
  return trace_is_inline_thread_ref(thread_ref) || trace_is_unknown_thread_ref(thread_ref)
             ? WordsToBytes(2)
             : 0u;
}

size_t SizeOfEncodedArgValue(const trace_arg_value_t* arg_value) {
  switch (arg_value->type) {
    case TRACE_ARG_NULL:
      return 0u;
    case TRACE_ARG_BOOL:
      return 0u;  // stored inline
    case TRACE_ARG_INT32:
      return 0u;  // stored inline
    case TRACE_ARG_UINT32:
      return 0u;  // stored inline
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
  return sizeof(ArgumentHeader) + SizeOfEncodedStringRef(&arg->name_ref) +
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
      : ptr_(rqst_durable && context->UsingDurableBuffer() ? context->AllocDurableRecord(num_bytes)
                                                           : context->AllocRecord(num_bytes)) {}

  explicit operator bool() const { return ptr_ != nullptr; }

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
      WriteBytes(string_ref->inline_string, trace_inline_string_ref_length(string_ref));
    }
    return *this;
  }

  Payload& WriteThreadRef(const trace_thread_ref_t* thread_ref) {
    // TODO(fxbug.dev/30974): Unknown thread refs should not be stored inline.
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
      case TRACE_ARG_BOOL:
        WriteArgumentHeaderAndName(ArgumentType::kBool, &arg->name_ref, 0u,
                                   BoolArgumentFields::Value::Make(arg->value.bool_value));
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
        WriteArgumentHeaderAndName(
            ArgumentType::kString, &arg->name_ref,
            SizeOfEncodedStringRef(&arg->value.string_value_ref),
            StringArgumentFields::Index::Make(arg->value.string_value_ref.encoded_value));
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
  void WriteArgumentHeaderAndName(ArgumentType type, const trace_string_ref_t* name_ref,
                                  size_t content_size, uint64_t header_bits) {
    const size_t argument_size =
        sizeof(ArgumentHeader) + SizeOfEncodedStringRef(name_ref) + content_size;
    WriteUint64(MakeArgumentHeader(type, argument_size, name_ref) | header_bits);
    WriteStringRef(name_ref);
  }

  uint64_t* ptr_;
};

Payload WriteEventRecordBase(trace_context_t* context, EventType event_type,
                             trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
                             const trace_string_ref_t* category_ref,
                             const trace_string_ref_t* name_ref, const trace_arg_t* args,
                             size_t num_args, size_t content_size) {
  const size_t record_size =
      sizeof(RecordHeader) + WordsToBytes(1) + SizeOfEncodedThreadRef(thread_ref) +
      SizeOfEncodedStringRef(category_ref) + SizeOfEncodedStringRef(name_ref) +
      SizeOfEncodedArgs(args, num_args) + content_size;
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
                       trace_string_index_t index, const char* string, size_t length) {
  ZX_DEBUG_ASSERT(index != TRACE_ENCODED_STRING_REF_EMPTY);
  ZX_DEBUG_ASSERT(index <= TRACE_ENCODED_STRING_REF_MAX_INDEX);

  if (unlikely(length > TRACE_ENCODED_STRING_REF_MAX_LENGTH))
    length = TRACE_ENCODED_STRING_REF_MAX_LENGTH;

  const size_t record_size = sizeof(trace::RecordHeader) + trace::Pad(length);
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

bool WriteThreadRecord(trace_context_t* context, trace_thread_index_t index, zx_koid_t process_koid,
                       zx_koid_t thread_koid) {
  ZX_DEBUG_ASSERT(index != TRACE_ENCODED_THREAD_REF_INLINE);
  ZX_DEBUG_ASSERT(index <= TRACE_ENCODED_THREAD_REF_MAX_INDEX);

  const size_t record_size = sizeof(trace::RecordHeader) + trace::WordsToBytes(2);
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

bool RegisterString(trace_context_t* context, const char* string_literal, bool check_category,
                    trace_string_ref_t* out_ref_optional) {
  if (unlikely(!string_literal || !*string_literal)) {
    if (check_category)
      return false;  // NULL and empty strings are not valid categories
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
          entry->flags |= StringEntry::kCategoryChecked | StringEntry::kCategoryEnabled;
        } else {
          entry->flags |= StringEntry::kCategoryChecked;
        }
      }
      if (!(entry->flags & StringEntry::kCategoryEnabled)) {
        return false;  // category disabled
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
                   WriteStringRecord(context, rqst_durable, entry->index, string_literal,
                                     string_len))) {
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
  // TODO(fxbug.dev/30978): Since we can't use the thread-local cache here, cache
  // this registered string on the trace context structure, guarded by a mutex.
  // Make sure to assign it a string index if possible instead of inlining.
  if (check_category && !CheckCategory(context, string_literal)) {
    return false;  // category disabled
  }
  if (out_ref_optional) {
    *out_ref_optional = trace_make_inline_c_string_ref(string_literal);
  }
  return true;
}

}  // namespace
}  // namespace trace

EXPORT bool trace_context_is_category_enabled(trace_context_t* context,
                                              const char* category_literal) {
  return trace::RegisterString(context, category_literal, true, nullptr);
}

EXPORT_NO_DDK void trace_context_register_string_copy(trace_context_t* context, const char* string,
                                                      size_t length, trace_string_ref_t* out_ref) {
  // TODO(fxbug.dev/30978): Cache the registered strings on the trace context structure,
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

EXPORT void trace_context_register_string_literal(trace_context_t* context,
                                                  const char* string_literal,
                                                  trace_string_ref_t* out_ref) {
  bool result = trace::RegisterString(context, string_literal, false, out_ref);
  ZX_DEBUG_ASSERT(result);
}

EXPORT_NO_DDK bool trace_context_register_category_literal(trace_context_t* context,
                                                           const char* category_literal,
                                                           trace_string_ref_t* out_ref) {
  return trace::RegisterString(context, category_literal, true, out_ref);
}

EXPORT void trace_context_register_current_thread(trace_context_t* context,
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
  trace_context_write_thread_info_record(context, process_koid, thread_koid, &name_ref);

  if (likely(cache)) {
    trace_thread_index_t index;
    // If allocating an index succeeds but writing the record fails,
    // toss the index and return an inline reference. The index is lost
    // anyway, but the result won't be half-complete. The subsequent
    // write of the inlined reference will likely also fail, but that's ok.
    if (likely(context->AllocThreadIndex(&index) &&
               trace::WriteThreadRecord(context, index, process_koid, thread_koid))) {
      cache->thread_ref = trace_make_indexed_thread_ref(index);
    } else {
      cache->thread_ref = trace_make_inline_thread_ref(process_koid, thread_koid);
    }
    *out_ref = cache->thread_ref;
    return;
  }

  // Slow path: the context's generation is out of date so we can't
  // cache anything related to the current thread.
  trace_context_register_thread(context, trace::GetCurrentProcessKoid(),
                                trace::GetCurrentThreadKoid(), out_ref);
}

EXPORT_NO_DDK void trace_context_register_thread(trace_context_t* context, zx_koid_t process_koid,
                                                 zx_koid_t thread_koid,
                                                 trace_thread_ref_t* out_ref) {
  // TODO(fxbug.dev/30978): Since we can't use the thread-local cache here, cache
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

EXPORT void trace_context_register_vthread(trace_context_t* context, zx_koid_t process_koid,
                                           const char* vthread_literal,
                                           trace_vthread_id_t vthread_id,
                                           trace_thread_ref_t* out_ref) {
  zx_koid_t vthread_koid = trace::MakeArtificialKoid(vthread_id);

  trace::ThreadEntry* entry = trace::CacheThreadEntry(context->generation(), vthread_koid);
  if (likely(entry && !trace_is_unknown_thread_ref(&entry->thread_ref))) {
    // Fast path: the thread is already registered.
    *out_ref = entry->thread_ref;
    return;
  }

  if (process_koid == ZX_KOID_INVALID) {
    process_koid = trace::GetCurrentProcessKoid();
  }

  trace_string_ref name_ref = trace_make_inline_c_string_ref(vthread_literal);
  trace_context_write_thread_info_record(context, process_koid, vthread_koid, &name_ref);

  if (likely(entry)) {
    trace_thread_index_t index;
    // If allocating an index succeeds but writing the record fails,
    // toss the index and return an inline reference. The index is lost
    // anyway, but the result won't be half-complete. The subsequent
    // write of the inlined reference will likely also fail, but that's ok.
    if (likely(context->AllocThreadIndex(&index) &&
               trace::WriteThreadRecord(context, index, process_koid, vthread_koid))) {
      entry->thread_ref = trace_make_indexed_thread_ref(index);
    } else {
      entry->thread_ref = trace_make_inline_thread_ref(process_koid, vthread_koid);
    }
    *out_ref = entry->thread_ref;
    return;
  }

  *out_ref = trace_make_inline_thread_ref(process_koid, vthread_koid);
}

EXPORT void* trace_context_begin_write_blob_record(trace_context_t* context, trace_blob_type_t type,
                                                   const trace_string_ref_t* name_ref,
                                                   size_t blob_size) {
  const size_t name_string_size = trace::SizeOfEncodedStringRef(name_ref);
  const size_t record_size_less_blob = sizeof(trace::RecordHeader) + name_string_size;
  const size_t padded_blob_size = trace::Pad(blob_size);
  const size_t max_record_size = trace::RecordFields::kMaxRecordSizeBytes;
  if (record_size_less_blob > max_record_size ||
      padded_blob_size > max_record_size - record_size_less_blob) {
    return nullptr;
  }
  const size_t record_size = record_size_less_blob + padded_blob_size;
  trace::Payload payload(context, record_size);
  if (payload) {
    auto blob =
        payload
            .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kBlob, record_size) |
                         trace::BlobRecordFields::BlobType::Make(trace::ToUnderlyingType(type)) |
                         trace::BlobRecordFields::NameStringRef::Make(name_ref->encoded_value) |
                         trace::BlobRecordFields::BlobSize::Make(blob_size))
            .WriteStringRef(name_ref)
            .PrepareWriteBytes(blob_size);
    return blob;
  } else {
    return nullptr;
  }
}

EXPORT void trace_context_write_blob_record(trace_context_t* context, trace_blob_type_t type,
                                            const trace_string_ref_t* name_ref, const void* blob,
                                            size_t blob_size) {
  auto buf = trace_context_begin_write_blob_record(context, type, name_ref, blob_size);
  if (buf) {
    memcpy(buf, blob, blob_size);
  }
}

EXPORT void trace_context_send_alert(trace_context_t* context, const char* alert_name) {
  context->handler()->ops->send_alert(context->handler(), alert_name);
}

void trace_context_write_kernel_object_record(trace_context_t* context, bool use_durable,
                                              zx_koid_t koid, zx_obj_type_t type,
                                              const trace_string_ref_t* name_ref,
                                              const trace_arg_t* args, size_t num_args) {
  const size_t record_size = sizeof(trace::RecordHeader) + trace::WordsToBytes(1) +
                             trace::SizeOfEncodedStringRef(name_ref) +
                             trace::SizeOfEncodedArgs(args, num_args);
  trace::Payload payload(context, use_durable, record_size);
  if (payload) {
    payload
        .WriteUint64(trace::MakeRecordHeader(trace::RecordType::kKernelObject, record_size) |
                     trace::KernelObjectRecordFields::ObjectType::Make(type) |
                     trace::KernelObjectRecordFields::NameStringRef::Make(name_ref->encoded_value) |
                     trace::KernelObjectRecordFields::ArgumentCount::Make(num_args))
        .WriteUint64(koid)
        .WriteStringRef(name_ref)
        .WriteArgs(args, num_args);
  }
}

EXPORT void trace_context_write_kernel_object_record_for_handle(trace_context_t* context,
                                                                zx_handle_t handle,
                                                                const trace_arg_t* args,
                                                                size_t num_args) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK)
    return;

  trace_string_ref name_ref;
  char name_buf[ZX_MAX_NAME_LEN];
  trace::GetObjectName(handle, name_buf, sizeof(name_buf), &name_ref);

  zx_obj_type_t obj_type = static_cast<zx_obj_type_t>(info.type);
  switch (obj_type) {
    case ZX_OBJ_TYPE_PROCESS:
      // TODO(fxbug.dev/30972): Support custom args.
      trace_context_write_process_info_record(context, info.koid, &name_ref);
      break;
    case ZX_OBJ_TYPE_THREAD:
      // TODO(fxbug.dev/30972): Support custom args.
      trace_context_write_thread_info_record(context, info.related_koid, info.koid, &name_ref);
      break;
    default:
      trace_context_write_kernel_object_record(context, false, info.koid, obj_type, &name_ref, args,
                                               num_args);
      break;
  }
}

EXPORT_NO_DDK void trace_context_write_process_info_record(
    trace_context_t* context, zx_koid_t process_koid, const trace_string_ref_t* process_name_ref) {
  trace_context_write_kernel_object_record(context, true, process_koid, ZX_OBJ_TYPE_PROCESS,
                                           process_name_ref, nullptr, 0u);
}

EXPORT_NO_DDK void trace_context_write_thread_info_record(
    trace_context_t* context, zx_koid_t process_koid, zx_koid_t thread_koid,
    const trace_string_ref_t* thread_name_ref) {
  // TODO(fxbug.dev/30972): We should probably store the related koid in the trace
  // event directly instead of packing it into an argument like this.
  trace_arg_t arg;
  trace_context_register_string_literal(context, "process", &arg.name_ref);
  arg.value.type = TRACE_ARG_KOID;
  arg.value.koid_value = process_koid;
  trace_context_write_kernel_object_record(context, true, thread_koid, ZX_OBJ_TYPE_THREAD,
                                           thread_name_ref, &arg, 1u);
}

EXPORT_NO_DDK void trace_context_write_context_switch_record(
    trace_context_t* context, trace_ticks_t event_time, trace_cpu_number_t cpu_number,
    trace_thread_state_t outgoing_thread_state, const trace_thread_ref_t* outgoing_thread_ref,
    const trace_thread_ref_t* incoming_thread_ref, trace_thread_priority_t outgoing_thread_priority,
    trace_thread_priority_t incoming_thread_priority) {
  const size_t record_size = sizeof(trace::RecordHeader) + trace::WordsToBytes(1) +
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

EXPORT_NO_DDK void trace_context_write_log_record(trace_context_t* context,
                                                  trace_ticks_t event_time,
                                                  const trace_thread_ref_t* thread_ref,
                                                  const char* log_message,
                                                  size_t log_message_length) {
  if (!log_message)
    return;

  log_message_length =
      std::min(log_message_length, size_t(trace::LogRecordFields::kMaxMessageLength));
  const size_t record_size = sizeof(trace::RecordHeader) +
                             trace::SizeOfEncodedThreadRef(thread_ref) + trace::WordsToBytes(1) +
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

EXPORT void trace_context_write_instant_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref, trace_scope_t scope,
    const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kInstant, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(trace::ToUnderlyingType(scope));
  }
}

EXPORT void trace_context_write_counter_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_counter_id_t counter_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kCounter, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(counter_id);
  }
}

EXPORT void trace_context_write_duration_event_record(
    trace_context_t* context, trace_ticks_t start_time, trace_ticks_t end_time,
    const trace_thread_ref_t* thread_ref, const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kDurationComplete, start_time,
                                  thread_ref, category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(end_time);
  }
}

EXPORT void trace_context_write_duration_begin_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
  trace::WriteEventRecordBase(context, trace::EventType::kDurationBegin, event_time, thread_ref,
                              category_ref, name_ref, args, num_args, 0u);
}

EXPORT void trace_context_write_duration_end_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args) {
  trace::WriteEventRecordBase(context, trace::EventType::kDurationEnd, event_time, thread_ref,
                              category_ref, name_ref, args, num_args, 0u);
}

EXPORT void trace_context_write_async_begin_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_async_id_t async_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kAsyncBegin, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(async_id);
  }
}

EXPORT void trace_context_write_async_instant_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_async_id_t async_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kAsyncInstant, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(async_id);
  }
}

EXPORT void trace_context_write_async_end_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_async_id_t async_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kAsyncEnd, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(async_id);
  }
}

EXPORT void trace_context_write_flow_begin_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kFlowBegin, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(flow_id);
  }
}

EXPORT void trace_context_write_flow_step_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kFlowStep, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(flow_id);
  }
}

EXPORT void trace_context_write_flow_end_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id, const trace_arg_t* args, size_t num_args) {
  const size_t content_size = trace::WordsToBytes(1);
  trace::Payload payload =
      trace::WriteEventRecordBase(context, trace::EventType::kFlowEnd, event_time, thread_ref,
                                  category_ref, name_ref, args, num_args, content_size);
  if (payload) {
    payload.WriteUint64(flow_id);
  }
}

trace::Payload trace_context_begin_write_large_blob_record(trace_context_t* context,
                                                           trace_blob_format_t format,
                                                           size_t content_size) {
  const size_t record_size = sizeof(trace::RecordHeader) + content_size;

  trace::Payload payload(context, record_size);
  if (payload) {
    payload.WriteUint64(
        trace::LargeBlobFields::Type::Make(ToUnderlyingType(trace::RecordType::kLargeRecord)) |
        trace::LargeBlobFields::RecordSize::Make(trace::BytesToWords(record_size)) |
        trace::LargeBlobFields::LargeType::Make(ToUnderlyingType(trace::LargeRecordType::kBlob)) |
        trace::LargeBlobFields::BlobFormat::Make(format));
  }
  return payload;
}

EXPORT void trace_context_write_blob_event_record(
    trace_context_t* context, trace_ticks_t event_time, const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref, const trace_string_ref_t* name_ref, const void* blob,
    size_t blob_size, const trace_arg_t* args, size_t num_args) {
  const size_t content_size =
      trace::WordsToBytes(1) + trace::SizeOfEncodedStringRef(category_ref) +
      trace::SizeOfEncodedStringRef(name_ref) + trace::WordsToBytes(1) +  // event time
      trace::SizeOfEncodedThreadRef(thread_ref) + trace::SizeOfEncodedArgs(args, num_args) +
      trace::WordsToBytes(1) +  // blob size
      trace::Pad(blob_size);

  trace::Payload payload =
      trace_context_begin_write_large_blob_record(context, TRACE_BLOB_FORMAT_EVENT, content_size);
  if (payload) {
    payload
        .WriteUint64(
            trace::BlobFormatEventFields::CategoryStringRef::Make(category_ref->encoded_value) |
            trace::BlobFormatEventFields::NameStringRef::Make(name_ref->encoded_value) |
            trace::BlobFormatEventFields::ArgumentCount::Make(num_args) |
            trace::BlobFormatEventFields::ThreadRef::Make(thread_ref->encoded_value))
        .WriteStringRef(category_ref)
        .WriteStringRef(name_ref)
        .WriteUint64(event_time)
        .WriteThreadRef(thread_ref)
        .WriteArgs(args, num_args)
        .WriteUint64(blob_size)
        .WriteBytes(blob, blob_size);
  }
}

EXPORT void trace_context_write_blob_attachment_record(trace_context_t* context,
                                                       const trace_string_ref_t* category_ref,
                                                       const trace_string_ref_t* name_ref,
                                                       const void* blob, size_t blob_size) {
  const size_t content_size = trace::WordsToBytes(1) +  // format header
                              trace::SizeOfEncodedStringRef(category_ref) +
                              trace::SizeOfEncodedStringRef(name_ref) +
                              trace::WordsToBytes(1) +  // blob size
                              trace::Pad(blob_size);

  trace::Payload payload = trace_context_begin_write_large_blob_record(
      context, TRACE_BLOB_FORMAT_ATTACHMENT, content_size);
  if (payload) {
    payload
        .WriteUint64(
            trace::BlobFormatAttachmentFields::CategoryStringRef::Make(
                category_ref->encoded_value) |
            trace::BlobFormatAttachmentFields::NameStringRef::Make(name_ref->encoded_value))
        .WriteStringRef(category_ref)
        .WriteStringRef(name_ref)
        .WriteUint64(blob_size)
        .WriteBytes(blob, blob_size);
  }
}

// TODO(dje): Move data to header?
EXPORT_NO_DDK void trace_context_write_initialization_record(trace_context_t* context,
                                                             zx_ticks_t ticks_per_second) {
  const size_t record_size = sizeof(trace::RecordHeader) + trace::WordsToBytes(1);
  trace::Payload payload(context, true, record_size);
  if (payload) {
    payload.WriteUint64(trace::MakeRecordHeader(trace::RecordType::kInitialization, record_size))
        .WriteUint64(ticks_per_second);
  }
}

EXPORT_NO_DDK void trace_context_write_string_record(trace_context_t* context,
                                                     trace_string_index_t index, const char* string,
                                                     size_t length) {
  if (unlikely(!trace::WriteStringRecord(context, false, index, string, length))) {
    // The write will fail if the buffer is full. Nothing we can do.
  }
}

EXPORT_NO_DDK void trace_context_write_thread_record(trace_context_t* context,
                                                     trace_thread_index_t index,
                                                     zx_koid_t process_koid,
                                                     zx_koid_t thread_koid) {
  if (unlikely(!trace::WriteThreadRecord(context, index, process_koid, thread_koid))) {
    // The write will fail if the buffer is full. Nothing we can do.
  }
}

EXPORT_NO_DDK void* trace_context_alloc_record(trace_context_t* context, size_t num_bytes) {
  return context->AllocRecord(num_bytes);
}

EXPORT_NO_DDK void trace_context_snapshot_buffer_header_internal(
    trace_prolonged_context_t* context, ::trace::internal::trace_buffer_header* header) {
  auto ctx = reinterpret_cast<trace_context_t*>(context);
  ctx->UpdateBufferHeaderAfterStopped();
  memcpy(header, ctx->buffer_header(), sizeof(*header));
}

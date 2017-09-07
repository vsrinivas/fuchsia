// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>

#include <fbl/macros.h>
#include <fbl/new.h>
#include <fbl/string.h>
#include <fbl/type_support.h>
#include <fbl/vector.h>
#include <trace-engine/types.h>

namespace trace {

// Holds a process koid and thread koid as a pair.
// Sorts by process koid then by thread koid.
class ProcessThread final {
public:
    constexpr ProcessThread()
        : process_koid_(MX_KOID_INVALID), thread_koid_(MX_KOID_INVALID) {}
    constexpr explicit ProcessThread(mx_koid_t process_koid, mx_koid_t thread_koid)
        : process_koid_(process_koid), thread_koid_(thread_koid) {}
    constexpr ProcessThread(const ProcessThread& other)
        : process_koid_(other.process_koid_), thread_koid_(other.thread_koid_) {}

    constexpr explicit operator bool() const {
        return thread_koid_ != 0u || process_koid_ != 0u;
    }

    constexpr bool operator==(const ProcessThread& other) const {
        return process_koid_ == other.process_koid_ &&
               thread_koid_ == other.thread_koid_;
    }

    constexpr bool operator!=(const ProcessThread& other) const {
        return !(*this == other);
    }

    constexpr bool operator<(const ProcessThread& other) const {
        if (process_koid_ != other.process_koid_) {
            return process_koid_ < other.process_koid_;
        }
        return thread_koid_ < other.thread_koid_;
    }

    constexpr mx_koid_t process_koid() const { return process_koid_; }
    constexpr mx_koid_t thread_koid() const { return thread_koid_; }

    ProcessThread& operator=(const ProcessThread& other) {
        process_koid_ = other.process_koid_;
        thread_koid_ = other.thread_koid_;
        return *this;
    }

    fbl::String ToString() const;

private:
    mx_koid_t process_koid_;
    mx_koid_t thread_koid_;
};

// A typed argument value.
class ArgumentValue final {
public:
    static ArgumentValue MakeNull() { return ArgumentValue(); }

    static ArgumentValue MakeInt32(int32_t value) { return ArgumentValue(value); }

    static ArgumentValue MakeUint32(uint32_t value) {
        return ArgumentValue(value);
    }

    static ArgumentValue MakeInt64(int64_t value) { return ArgumentValue(value); }

    static ArgumentValue MakeUint64(uint64_t value) {
        return ArgumentValue(value);
    }

    static ArgumentValue MakeDouble(double value) { return ArgumentValue(value); }

    static ArgumentValue MakeString(fbl::String value) {
        return ArgumentValue(fbl::move(value));
    }

    static ArgumentValue MakePointer(uint64_t value) {
        return ArgumentValue(PointerTag(), value);
    }

    static ArgumentValue MakeKoid(mx_koid_t value) {
        return ArgumentValue(KoidTag(), value);
    }

    ArgumentValue(ArgumentValue&& other) { MoveFrom(fbl::move(other)); }

    ~ArgumentValue() { Destroy(); }

    ArgumentValue& operator=(ArgumentValue&& other) {
        Destroy();
        MoveFrom(fbl::move(other));
        return *this;
    }

    ArgumentType type() const { return type_; }

    int32_t GetInt32() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kInt32);
        return int32_;
    }

    uint32_t GetUint32() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kUint32);
        return uint32_;
    }

    int64_t GetInt64() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kInt64);
        return int64_;
    }

    uint64_t GetUint64() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kUint64);
        return uint64_;
    }

    double GetDouble() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kDouble);
        return double_;
    }

    const fbl::String& GetString() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kString);
        return string_;
    }

    uint64_t GetPointer() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kPointer);
        return pointer_;
    }

    mx_koid_t GetKoid() const {
        MX_DEBUG_ASSERT(type_ == ArgumentType::kKoid);
        return koid_;
    }

    fbl::String ToString() const;

private:
    struct PointerTag {};
    struct KoidTag {};

    ArgumentValue()
        : type_(ArgumentType::kNull) {}

    explicit ArgumentValue(int32_t int32)
        : type_(ArgumentType::kInt32), int32_(int32) {}

    explicit ArgumentValue(uint32_t uint32)
        : type_(ArgumentType::kUint32), uint32_(uint32) {}

    explicit ArgumentValue(int64_t int64)
        : type_(ArgumentType::kInt64), int64_(int64) {}

    explicit ArgumentValue(uint64_t uint64)
        : type_(ArgumentType::kUint64), uint64_(uint64) {}

    explicit ArgumentValue(double d)
        : type_(ArgumentType::kDouble), double_(d) {}

    explicit ArgumentValue(fbl::String string)
        : type_(ArgumentType::kString) {
        new (&string_) fbl::String(fbl::move(string));
    }

    explicit ArgumentValue(PointerTag, uint64_t pointer)
        : type_(ArgumentType::kPointer), pointer_(pointer) {}

    explicit ArgumentValue(KoidTag, mx_koid_t koid)
        : type_(ArgumentType::kKoid), koid_(koid) {}

    void Destroy();
    void MoveFrom(ArgumentValue&& other);

    ArgumentType type_;
    union {
        int32_t int32_;
        uint32_t uint32_;
        int64_t int64_;
        uint64_t uint64_;
        double double_;
        fbl::String string_;
        uint64_t pointer_;
        mx_koid_t koid_;
    };

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ArgumentValue);
};

// Named argument and value.
class Argument final {
public:
    explicit Argument(fbl::String name, ArgumentValue value)
        : name_(fbl::move(name)), value_(fbl::move(value)) {}

    Argument(Argument&& other)
        : name_(fbl::move(other.name_)), value_(fbl::move(other.value_)) {}

    Argument& operator=(Argument&& other) {
        name_ = fbl::move(other.name_);
        value_ = fbl::move(other.value_);
        return *this;
    }

    const fbl::String& name() const { return name_; }
    const ArgumentValue& value() const { return value_; }

    fbl::String ToString() const;

private:
    fbl::String name_;
    ArgumentValue value_;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Argument);
};

// Metadata type specific data.
class MetadataContent final {
public:
    // Provider info event data.
    struct ProviderInfo {
        ProviderId id;
        fbl::String name;
    };

    // Provider section event data.
    struct ProviderSection {
        ProviderId id;
    };

    explicit MetadataContent(ProviderInfo provider_info)
        : type_(MetadataType::kProviderInfo), provider_info_(fbl::move(provider_info)) {}

    explicit MetadataContent(ProviderSection provider_section)
        : type_(MetadataType::kProviderSection),
          provider_section_(fbl::move(provider_section)) {}

    const ProviderInfo& GetProviderInfo() const {
        MX_DEBUG_ASSERT(type_ == MetadataType::kProviderInfo);
        return provider_info_;
    };

    const ProviderSection& GetProviderSection() const {
        MX_DEBUG_ASSERT(type_ == MetadataType::kProviderSection);
        return provider_section_;
    }

    MetadataContent(MetadataContent&& other)
        : type_(other.type_) { MoveFrom(fbl::move(other)); }

    ~MetadataContent() { Destroy(); }

    MetadataContent& operator=(MetadataContent&& other) {
        Destroy();
        MoveFrom(fbl::move(other));
        return *this;
    }

    MetadataType type() const { return type_; }

    fbl::String ToString() const;

private:
    void Destroy();
    void MoveFrom(MetadataContent&& other);

    MetadataType type_;
    union {
        ProviderInfo provider_info_;
        ProviderSection provider_section_;
    };

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MetadataContent);
};

// Event type specific data.
class EventData final {
public:
    // Instant event data.
    struct Instant {
        EventScope scope;
    };

    // Counter event data.
    struct Counter {
        trace_counter_id_t id;
    };

    // Duration begin event data.
    struct DurationBegin {};

    // Duration end event data.
    struct DurationEnd {};

    // Async begin event data.
    struct AsyncBegin {
        trace_async_id_t id;
    };

    // Async instant event data.
    struct AsyncInstant {
        trace_async_id_t id;
    };

    // Async end event data.
    struct AsyncEnd {
        trace_async_id_t id;
    };

    // Flow begin event data.
    struct FlowBegin {
        trace_flow_id_t id;
    };

    // Flow step event data.
    struct FlowStep {
        trace_flow_id_t id;
    };

    // Flow end event data.
    struct FlowEnd {
        trace_flow_id_t id;
    };

    explicit EventData(Instant instant)
        : type_(EventType::kInstant), instant_(fbl::move(instant)) {}

    explicit EventData(Counter counter)
        : type_(EventType::kCounter), counter_(fbl::move(counter)) {}

    explicit EventData(DurationBegin duration_begin)
        : type_(EventType::kDurationBegin), duration_begin_(fbl::move(duration_begin)) {}

    explicit EventData(DurationEnd duration_end)
        : type_(EventType::kDurationEnd), duration_end_(fbl::move(duration_end)) {}

    explicit EventData(AsyncBegin async_begin)
        : type_(EventType::kAsyncBegin), async_begin_(fbl::move(async_begin)) {}

    explicit EventData(AsyncInstant async_instant)
        : type_(EventType::kAsyncInstant), async_instant_(fbl::move(async_instant)) {}

    explicit EventData(AsyncEnd async_end)
        : type_(EventType::kAsyncEnd), async_end_(fbl::move(async_end)) {}

    explicit EventData(FlowBegin flow_begin)
        : type_(EventType::kFlowBegin), flow_begin_(fbl::move(flow_begin)) {}

    explicit EventData(FlowStep flow_step)
        : type_(EventType::kFlowStep), flow_step_(fbl::move(flow_step)) {}

    explicit EventData(FlowEnd flow_end)
        : type_(EventType::kFlowEnd), flow_end_(fbl::move(flow_end)) {}

    EventData(EventData&& other) { MoveFrom(fbl::move(other)); }

    EventData& operator=(EventData&& other) {
        Destroy();
        MoveFrom(fbl::move(other));
        return *this;
    }

    const Instant& GetInstant() const {
        MX_DEBUG_ASSERT(type_ == EventType::kInstant);
        return instant_;
    }

    const Counter& GetCounter() const {
        MX_DEBUG_ASSERT(type_ == EventType::kCounter);
        return counter_;
    }

    const DurationBegin& GetDurationBegin() const {
        MX_DEBUG_ASSERT(type_ == EventType::kDurationBegin);
        return duration_begin_;
    }

    const DurationEnd& GetDurationEnd() const {
        MX_DEBUG_ASSERT(type_ == EventType::kDurationEnd);
        return duration_end_;
    }

    const AsyncBegin& GetAsyncBegin() const {
        MX_DEBUG_ASSERT(type_ == EventType::kAsyncBegin);
        return async_begin_;
    };

    const AsyncInstant& GetAsyncInstant() const {
        MX_DEBUG_ASSERT(type_ == EventType::kAsyncInstant);
        return async_instant_;
    }

    const AsyncEnd& GetAsyncEnd() const {
        MX_DEBUG_ASSERT(type_ == EventType::kAsyncEnd);
        return async_end_;
    }

    const FlowBegin& GetFlowBegin() const {
        MX_DEBUG_ASSERT(type_ == EventType::kFlowBegin);
        return flow_begin_;
    }

    const FlowStep& GetFlowStep() const {
        MX_DEBUG_ASSERT(type_ == EventType::kFlowStep);
        return flow_step_;
    }

    const FlowEnd& GetFlowEnd() const {
        MX_DEBUG_ASSERT(type_ == EventType::kFlowEnd);
        return flow_end_;
    }

    EventType type() const { return type_; }

    fbl::String ToString() const;

private:
    void Destroy();
    void MoveFrom(EventData&& other);

    EventType type_;
    union {
        Instant instant_;
        Counter counter_;
        DurationBegin duration_begin_;
        DurationEnd duration_end_;
        AsyncBegin async_begin_;
        AsyncInstant async_instant_;
        AsyncEnd async_end_;
        FlowBegin flow_begin_;
        FlowStep flow_step_;
        FlowEnd flow_end_;
    };

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(EventData);
};

// A decoded record.
class Record final {
public:
    // Metadata record data.
    struct Metadata {
        MetadataType type() const { return content.type(); }
        MetadataContent content;
    };

    // Initialization record data.
    struct Initialization {
        trace_ticks_t ticks_per_second;
    };

    // String record data.
    struct String {
        trace_string_index_t index;
        fbl::String string;
    };

    // Thread record data.
    struct Thread {
        trace_thread_index_t index;
        ProcessThread process_thread;
    };

    // Event record data.
    struct Event {
        EventType type() const { return data.type(); }
        trace_ticks_t timestamp;
        ProcessThread process_thread;
        fbl::String category;
        fbl::String name;
        fbl::Vector<Argument> arguments;
        EventData data;
    };

    // Kernel Object record data.
    struct KernelObject {
        mx_koid_t koid;
        mx_obj_type_t object_type;
        fbl::String name;
        fbl::Vector<Argument> arguments;
    };

    // Context Switch record data.
    struct ContextSwitch {
        trace_ticks_t timestamp;
        trace_cpu_number_t cpu_number;
        ThreadState outgoing_thread_state;
        ProcessThread outgoing_thread;
        ProcessThread incoming_thread;
    };

    // Log record data.
    struct Log {
        trace_ticks_t timestamp;
        ProcessThread process_thread;
        fbl::String message;
    };

    explicit Record(Metadata record)
        : type_(RecordType::kMetadata), metadata_(fbl::move(record)) {}

    explicit Record(Initialization record)
        : type_(RecordType::kInitialization), initialization_(fbl::move(record)) {}

    explicit Record(String record)
        : type_(RecordType::kString) {
        new (&string_) String(fbl::move(record));
    }

    explicit Record(Thread record)
        : type_(RecordType::kThread) {
        new (&thread_) Thread(fbl::move(record));
    }

    explicit Record(Event record)
        : type_(RecordType::kEvent) {
        new (&event_) Event(fbl::move(record));
    }

    explicit Record(KernelObject record)
        : type_(RecordType::kKernelObject) {
        new (&kernel_object_) KernelObject(fbl::move(record));
    }

    explicit Record(ContextSwitch record)
        : type_(RecordType::kContextSwitch) {
        new (&context_switch_) ContextSwitch(fbl::move(record));
    }

    explicit Record(Log record)
        : type_(RecordType::kLog) {
        new (&log_) Log(fbl::move(record));
    }

    Record(Record&& other) { MoveFrom(fbl::move(other)); }

    ~Record() { Destroy(); }

    Record& operator=(Record&& other) {
        Destroy();
        MoveFrom(fbl::move(other));
        return *this;
    }

    const Metadata& GetMetadata() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kMetadata);
        return metadata_;
    }

    const Initialization& GetInitialization() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kInitialization);
        return initialization_;
    }

    const String& GetString() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kString);
        return string_;
    }

    const Thread& GetThread() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kThread);
        return thread_;
    };

    const Event& GetEvent() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kEvent);
        return event_;
    }

    const KernelObject& GetKernelObject() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kKernelObject);
        return kernel_object_;
    }

    const ContextSwitch& GetContextSwitch() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kContextSwitch);
        return context_switch_;
    }

    const Log& GetLog() const {
        MX_DEBUG_ASSERT(type_ == RecordType::kLog);
        return log_;
    }

    RecordType type() const { return type_; }

    fbl::String ToString() const;

private:
    void Destroy();
    void MoveFrom(Record&& other);

    RecordType type_;
    union {
        Metadata metadata_;
        Initialization initialization_;
        String string_;
        Thread thread_;
        Event event_;
        KernelObject kernel_object_;
        ContextSwitch context_switch_;
        Log log_;
    };

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Record);
};

} // namespace trace

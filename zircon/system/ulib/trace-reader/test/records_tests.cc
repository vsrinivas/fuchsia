// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <trace-reader/records.h>
#include <zxtest/zxtest.h>

namespace trace {
namespace {

template <typename T>
uint64_t ToWord(const T& value) {
  return *reinterpret_cast<const uint64_t*>(&value);
}

TEST(TraceRecords, ProcessThread) {
  trace::ProcessThread pt;
  EXPECT_EQ(ZX_KOID_INVALID, pt.process_koid());
  EXPECT_EQ(ZX_KOID_INVALID, pt.thread_koid());
  EXPECT_FALSE(!!pt);

  pt = trace::ProcessThread(0, 1);
  EXPECT_EQ(0, pt.process_koid());
  EXPECT_EQ(1, pt.thread_koid());
  EXPECT_TRUE(!!pt);

  pt = trace::ProcessThread(1, 0);
  EXPECT_EQ(1, pt.process_koid());
  EXPECT_EQ(0, pt.thread_koid());
  EXPECT_TRUE(!!pt);

  pt = trace::ProcessThread(trace::ProcessThread(4, 5));
  EXPECT_EQ(4, pt.process_koid());
  EXPECT_EQ(5, pt.thread_koid());
  EXPECT_TRUE(!!pt);

  EXPECT_TRUE(trace::ProcessThread(1, 2) == trace::ProcessThread(1, 2));
  EXPECT_FALSE(trace::ProcessThread(1, 2) == trace::ProcessThread(1, 4));
  EXPECT_FALSE(trace::ProcessThread(1, 2) == trace::ProcessThread(3, 2));
  EXPECT_FALSE(trace::ProcessThread(1, 2) == trace::ProcessThread(3, 4));

  EXPECT_FALSE(trace::ProcessThread(1, 2) != trace::ProcessThread(1, 2));
  EXPECT_TRUE(trace::ProcessThread(1, 2) != trace::ProcessThread(1, 4));
  EXPECT_TRUE(trace::ProcessThread(1, 2) != trace::ProcessThread(3, 2));
  EXPECT_TRUE(trace::ProcessThread(1, 2) != trace::ProcessThread(3, 4));

  EXPECT_FALSE(trace::ProcessThread(1, 2) < trace::ProcessThread(1, 2));
  EXPECT_FALSE(trace::ProcessThread(1, 2) < trace::ProcessThread(1, 1));
  EXPECT_TRUE(trace::ProcessThread(1, 2) < trace::ProcessThread(1, 3));
  EXPECT_TRUE(trace::ProcessThread(1, 2) < trace::ProcessThread(2, 2));
  EXPECT_TRUE(trace::ProcessThread(1, 2) < trace::ProcessThread(2, 3));

  EXPECT_FALSE(trace::ProcessThread() < trace::ProcessThread());
  EXPECT_TRUE(trace::ProcessThread() < trace::ProcessThread(1, 2));
  EXPECT_FALSE(trace::ProcessThread(1, 2) < trace::ProcessThread());

  EXPECT_STR_EQ("1/2", trace::ProcessThread(1, 2).ToString().c_str());
}

TEST(TraceRecords, ArgumentValue) {
  // null

  trace::ArgumentValue av = trace::ArgumentValue::MakeNull();
  EXPECT_EQ(trace::ArgumentType::kNull, av.type());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
  }

  EXPECT_STR_EQ("null", av.ToString().c_str());

  // bool

  av = trace::ArgumentValue::MakeBool(false);
  EXPECT_EQ(trace::ArgumentType::kBool, av.type());
  EXPECT_FALSE(av.GetBool());
  EXPECT_STR_EQ("bool(false)", av.ToString().c_str());

  av = trace::ArgumentValue::MakeBool(true);
  EXPECT_EQ(trace::ArgumentType::kBool, av.type());
  EXPECT_TRUE(av.GetBool());
  EXPECT_STR_EQ("bool(true)", av.ToString().c_str());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kBool, m.type());
    EXPECT_TRUE(m.GetBool());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kBool, av.type());
    EXPECT_TRUE(av.GetBool());
  }

  // int32

  av = trace::ArgumentValue::MakeInt32(INT32_MIN);
  EXPECT_EQ(trace::ArgumentType::kInt32, av.type());
  EXPECT_EQ(INT32_MIN, av.GetInt32());

  av = trace::ArgumentValue::MakeInt32(INT32_MAX);
  EXPECT_EQ(trace::ArgumentType::kInt32, av.type());
  EXPECT_EQ(INT32_MAX, av.GetInt32());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kInt32, m.type());
    EXPECT_EQ(INT32_MAX, m.GetInt32());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kInt32, av.type());
    EXPECT_EQ(INT32_MAX, av.GetInt32());
  }

  EXPECT_STR_EQ("int32(2147483647)", av.ToString().c_str());

  // uint32

  av = trace::ArgumentValue::MakeUint32(0);
  EXPECT_EQ(trace::ArgumentType::kUint32, av.type());
  EXPECT_EQ(0, av.GetUint32());

  av = trace::ArgumentValue::MakeUint32(UINT32_MAX);
  EXPECT_EQ(trace::ArgumentType::kUint32, av.type());
  EXPECT_EQ(UINT32_MAX, av.GetUint32());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kUint32, m.type());
    EXPECT_EQ(UINT32_MAX, m.GetUint32());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kUint32, av.type());
    EXPECT_EQ(UINT32_MAX, av.GetUint32());
  }

  EXPECT_STR_EQ("uint32(4294967295)", av.ToString().c_str());

  // int64

  av = trace::ArgumentValue::MakeInt64(INT64_MIN);
  EXPECT_EQ(trace::ArgumentType::kInt64, av.type());
  EXPECT_EQ(INT64_MIN, av.GetInt64());

  av = trace::ArgumentValue::MakeInt64(INT64_MAX);
  EXPECT_EQ(trace::ArgumentType::kInt64, av.type());
  EXPECT_EQ(INT64_MAX, av.GetInt64());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kInt64, m.type());
    EXPECT_EQ(INT64_MAX, m.GetInt64());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kInt64, av.type());
    EXPECT_EQ(INT64_MAX, av.GetInt64());
  }

  EXPECT_STR_EQ("int64(9223372036854775807)", av.ToString().c_str());

  // uint64

  av = trace::ArgumentValue::MakeUint64(0);
  EXPECT_EQ(trace::ArgumentType::kUint64, av.type());
  EXPECT_EQ(0, av.GetUint64());

  av = trace::ArgumentValue::MakeUint64(UINT64_MAX);
  EXPECT_EQ(trace::ArgumentType::kUint64, av.type());
  EXPECT_EQ(UINT64_MAX, av.GetUint64());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kUint64, m.type());
    EXPECT_EQ(UINT64_MAX, m.GetUint64());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kUint64, av.type());
    EXPECT_EQ(UINT64_MAX, av.GetUint64());
  }

  EXPECT_STR_EQ("uint64(18446744073709551615)", av.ToString().c_str());

  // double

  av = trace::ArgumentValue::MakeDouble(-3.14);
  EXPECT_EQ(trace::ArgumentType::kDouble, av.type());
  EXPECT_EQ(-3.14, av.GetDouble());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kDouble, m.type());
    EXPECT_EQ(-3.14, m.GetDouble());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kDouble, av.type());
    EXPECT_EQ(-3.14, av.GetDouble());
  }

  EXPECT_STR_EQ("double(-3.140000)", av.ToString().c_str());

  // string

  av = trace::ArgumentValue::MakeString("Hello World!");
  EXPECT_EQ(trace::ArgumentType::kString, av.type());
  EXPECT_TRUE(av.GetString() == "Hello World!");

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kString, m.type());
    EXPECT_TRUE(m.GetString() == "Hello World!");

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kString, av.type());
    EXPECT_TRUE(av.GetString() == "Hello World!");
  }

  EXPECT_STR_EQ("string(\"Hello World!\")", av.ToString().c_str());

  // pointer

  av = trace::ArgumentValue::MakePointer(0);
  EXPECT_EQ(trace::ArgumentType::kPointer, av.type());
  EXPECT_EQ(0, av.GetPointer());

  av = trace::ArgumentValue::MakePointer(UINTPTR_MAX);
  EXPECT_EQ(trace::ArgumentType::kPointer, av.type());
  EXPECT_EQ(UINTPTR_MAX, av.GetPointer());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kPointer, m.type());
    EXPECT_EQ(UINTPTR_MAX, m.GetPointer());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kPointer, av.type());
    EXPECT_EQ(UINTPTR_MAX, av.GetPointer());
  }

  EXPECT_STR_EQ("pointer(0xffffffffffffffff)", av.ToString().c_str());

  // koid

  av = trace::ArgumentValue::MakeKoid(ZX_KOID_INVALID);
  EXPECT_EQ(trace::ArgumentType::kKoid, av.type());
  EXPECT_EQ(ZX_KOID_INVALID, av.GetKoid());

  av = trace::ArgumentValue::MakeKoid(UINT64_MAX);
  EXPECT_EQ(trace::ArgumentType::kKoid, av.type());
  EXPECT_EQ(UINT64_MAX, av.GetKoid());

  {
    trace::ArgumentValue m(std::move(av));
    EXPECT_EQ(trace::ArgumentType::kNull, av.type());
    EXPECT_EQ(trace::ArgumentType::kKoid, m.type());
    EXPECT_EQ(UINT64_MAX, m.GetKoid());

    av = std::move(m);
    EXPECT_EQ(trace::ArgumentType::kNull, m.type());
    EXPECT_EQ(trace::ArgumentType::kKoid, av.type());
    EXPECT_EQ(UINT64_MAX, av.GetKoid());
  }

  EXPECT_STR_EQ("koid(18446744073709551615)", av.ToString().c_str());
}

TEST(TraceRecords, Argument) {
  trace::Argument a("name", trace::ArgumentValue::MakeInt32(123));
  EXPECT_TRUE(a.name() == "name");
  EXPECT_EQ(123, a.value().GetInt32());

  trace::Argument m(std::move(a));
  EXPECT_TRUE(a.name().empty());
  EXPECT_EQ(trace::ArgumentType::kNull, a.value().type());
  EXPECT_TRUE(m.name() == "name");
  EXPECT_EQ(123, m.value().GetInt32());

  a = std::move(m);
  EXPECT_TRUE(m.name().empty());
  EXPECT_EQ(trace::ArgumentType::kNull, m.value().type());
  EXPECT_TRUE(a.name() == "name");
  EXPECT_EQ(123, a.value().GetInt32());

  EXPECT_STR_EQ("name: int32(123)", a.ToString().c_str());
}

TEST(TraceRecords, MetadataData) {
  // provider info

  {
    trace::MetadataContent d(trace::MetadataContent::ProviderInfo{1, "provider"});
    EXPECT_EQ(trace::MetadataType::kProviderInfo, d.type());
    EXPECT_EQ(1, d.GetProviderInfo().id);
    EXPECT_TRUE(d.GetProviderInfo().name == "provider");

    trace::MetadataContent m(std::move(d));
    EXPECT_EQ(trace::MetadataType::kProviderInfo, m.type());
    EXPECT_EQ(1, m.GetProviderInfo().id);
    EXPECT_TRUE(m.GetProviderInfo().name == "provider");

    d = std::move(m);
    EXPECT_EQ(trace::MetadataType::kProviderInfo, d.type());
    EXPECT_EQ(1, d.GetProviderInfo().id);
    EXPECT_TRUE(d.GetProviderInfo().name == "provider");

    EXPECT_STR_EQ("ProviderInfo(id: 1, name: \"provider\")", d.ToString().c_str());
  }

  // provider section

  {
    trace::MetadataContent d(trace::MetadataContent::ProviderSection{1});
    EXPECT_EQ(trace::MetadataType::kProviderSection, d.type());
    EXPECT_EQ(1, d.GetProviderSection().id);

    trace::MetadataContent m(std::move(d));
    EXPECT_EQ(trace::MetadataType::kProviderSection, m.type());
    EXPECT_EQ(1, m.GetProviderSection().id);

    d = std::move(m);
    EXPECT_EQ(trace::MetadataType::kProviderSection, d.type());
    EXPECT_EQ(1, d.GetProviderSection().id);

    EXPECT_STR_EQ("ProviderSection(id: 1)", d.ToString().c_str());
  }
}

TEST(TraceRecords, EventData) {
  // instant

  {
    trace::EventData d(trace::EventData::Instant{trace::EventScope::kGlobal});
    EXPECT_EQ(trace::EventType::kInstant, d.type());
    EXPECT_EQ(trace::EventScope::kGlobal, d.GetInstant().scope);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kInstant, m.type());
    EXPECT_EQ(trace::EventScope::kGlobal, m.GetInstant().scope);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kInstant, d.type());
    EXPECT_EQ(trace::EventScope::kGlobal, d.GetInstant().scope);

    EXPECT_STR_EQ("Instant(scope: global)", d.ToString().c_str());
  }

  // counter

  {
    trace::EventData d(trace::EventData::Counter{123});
    EXPECT_EQ(trace::EventType::kCounter, d.type());
    EXPECT_EQ(123, d.GetCounter().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kCounter, m.type());
    EXPECT_EQ(123, m.GetCounter().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kCounter, d.type());
    EXPECT_EQ(123, d.GetCounter().id);

    EXPECT_STR_EQ("Counter(id: 123)", d.ToString().c_str());
  }

  // duration begin

  {
    trace::EventData d(trace::EventData::DurationBegin{});
    EXPECT_EQ(trace::EventType::kDurationBegin, d.type());
    EXPECT_NOT_NULL(&d.GetDurationBegin());

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kDurationBegin, m.type());
    EXPECT_NOT_NULL(&m.GetDurationBegin());

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kDurationBegin, d.type());
    EXPECT_NOT_NULL(&d.GetDurationBegin());

    EXPECT_STR_EQ("DurationBegin", d.ToString().c_str());
  }

  // duration end

  {
    trace::EventData d(trace::EventData::DurationEnd{});
    EXPECT_EQ(trace::EventType::kDurationEnd, d.type());
    EXPECT_NOT_NULL(&d.GetDurationEnd());

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kDurationEnd, m.type());
    EXPECT_NOT_NULL(&m.GetDurationEnd());

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kDurationEnd, d.type());
    EXPECT_NOT_NULL(&d.GetDurationEnd());

    EXPECT_STR_EQ("DurationEnd", d.ToString().c_str());
  }

  // duration complete

  {
    trace::EventData d(trace::EventData::DurationComplete{123});
    EXPECT_EQ(trace::EventType::kDurationComplete, d.type());
    EXPECT_EQ(123, d.GetDurationComplete().end_time);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kDurationComplete, m.type());
    EXPECT_EQ(123, m.GetDurationComplete().end_time);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kDurationComplete, d.type());
    EXPECT_EQ(123, d.GetDurationComplete().end_time);

    EXPECT_STR_EQ("DurationComplete(end_ts: 123)", d.ToString().c_str());
  }

  // async begin

  {
    trace::EventData d(trace::EventData::AsyncBegin{123});
    EXPECT_EQ(trace::EventType::kAsyncBegin, d.type());
    EXPECT_EQ(123, d.GetAsyncBegin().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kAsyncBegin, m.type());
    EXPECT_EQ(123, m.GetAsyncBegin().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kAsyncBegin, d.type());
    EXPECT_EQ(123, d.GetAsyncBegin().id);

    EXPECT_STR_EQ("AsyncBegin(id: 123)", d.ToString().c_str());
  }

  // async instant

  {
    trace::EventData d(trace::EventData::AsyncInstant{123});
    EXPECT_EQ(trace::EventType::kAsyncInstant, d.type());
    EXPECT_EQ(123, d.GetAsyncInstant().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kAsyncInstant, m.type());
    EXPECT_EQ(123, m.GetAsyncInstant().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kAsyncInstant, d.type());
    EXPECT_EQ(123, d.GetAsyncInstant().id);

    EXPECT_STR_EQ("AsyncInstant(id: 123)", d.ToString().c_str());
  }

  // async end

  {
    trace::EventData d(trace::EventData::AsyncEnd{123});
    EXPECT_EQ(trace::EventType::kAsyncEnd, d.type());
    EXPECT_EQ(123, d.GetAsyncEnd().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kAsyncEnd, m.type());
    EXPECT_EQ(123, m.GetAsyncEnd().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kAsyncEnd, d.type());
    EXPECT_EQ(123, d.GetAsyncEnd().id);

    EXPECT_STR_EQ("AsyncEnd(id: 123)", d.ToString().c_str());
  }

  // flow begin

  {
    trace::EventData d(trace::EventData::FlowBegin{123});
    EXPECT_EQ(trace::EventType::kFlowBegin, d.type());
    EXPECT_EQ(123, d.GetFlowBegin().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kFlowBegin, m.type());
    EXPECT_EQ(123, m.GetFlowBegin().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kFlowBegin, d.type());
    EXPECT_EQ(123, d.GetFlowBegin().id);

    EXPECT_STR_EQ("FlowBegin(id: 123)", d.ToString().c_str());
  }

  // flow step

  {
    trace::EventData d(trace::EventData::FlowStep{123});
    EXPECT_EQ(trace::EventType::kFlowStep, d.type());
    EXPECT_EQ(123, d.GetFlowStep().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kFlowStep, m.type());
    EXPECT_EQ(123, m.GetFlowStep().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kFlowStep, d.type());
    EXPECT_EQ(123, d.GetFlowStep().id);

    EXPECT_STR_EQ("FlowStep(id: 123)", d.ToString().c_str());
  }

  // flow end

  {
    trace::EventData d(trace::EventData::FlowEnd{123});
    EXPECT_EQ(trace::EventType::kFlowEnd, d.type());
    EXPECT_EQ(123, d.GetFlowEnd().id);

    trace::EventData m(std::move(d));
    EXPECT_EQ(trace::EventType::kFlowEnd, m.type());
    EXPECT_EQ(123, m.GetFlowEnd().id);

    d = std::move(m);
    EXPECT_EQ(trace::EventType::kFlowEnd, d.type());
    EXPECT_EQ(123, d.GetFlowEnd().id);

    EXPECT_STR_EQ("FlowEnd(id: 123)", d.ToString().c_str());
  }
}

TEST(TraceRecords, Record) {
  // metadata

  {
    trace::Record r(trace::Record::Metadata{
        trace::MetadataContent(trace::MetadataContent::ProviderSection{123})});
    EXPECT_EQ(trace::RecordType::kMetadata, r.type());
    EXPECT_EQ(trace::MetadataType::kProviderSection, r.GetMetadata().type());
    EXPECT_EQ(123, r.GetMetadata().content.GetProviderSection().id);

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kMetadata, m.type());
    EXPECT_EQ(trace::MetadataType::kProviderSection, m.GetMetadata().type());
    EXPECT_EQ(123, m.GetMetadata().content.GetProviderSection().id);

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kMetadata, r.type());
    EXPECT_EQ(trace::MetadataType::kProviderSection, r.GetMetadata().type());
    EXPECT_EQ(123, r.GetMetadata().content.GetProviderSection().id);

    EXPECT_STR_EQ("Metadata(content: ProviderSection(id: 123))", r.ToString().c_str());
  }

  // initialization

  {
    trace::Record r(trace::Record::Initialization{123});
    EXPECT_EQ(trace::RecordType::kInitialization, r.type());
    EXPECT_EQ(123, r.GetInitialization().ticks_per_second);

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kInitialization, m.type());
    EXPECT_EQ(123, m.GetInitialization().ticks_per_second);

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kInitialization, r.type());
    EXPECT_EQ(123, r.GetInitialization().ticks_per_second);

    EXPECT_STR_EQ("Initialization(ticks_per_second: 123)", r.ToString().c_str());
  }

  // string

  {
    trace::Record r(trace::Record::String{123, "hi!"});
    EXPECT_EQ(trace::RecordType::kString, r.type());
    EXPECT_EQ(123, r.GetString().index);
    EXPECT_TRUE(r.GetString().string == "hi!");

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kString, m.type());
    EXPECT_EQ(123, m.GetString().index);
    EXPECT_TRUE(m.GetString().string == "hi!");

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kString, r.type());
    EXPECT_EQ(123, r.GetString().index);
    EXPECT_TRUE(r.GetString().string == "hi!");

    EXPECT_STR_EQ("String(index: 123, \"hi!\")", r.ToString().c_str());
  }

  // thread

  {
    trace::Record r(trace::Record::Thread{123, trace::ProcessThread(4, 5)});
    EXPECT_EQ(trace::RecordType::kThread, r.type());
    EXPECT_EQ(123, r.GetThread().index);
    EXPECT_EQ(4, r.GetThread().process_thread.process_koid());
    EXPECT_EQ(5, r.GetThread().process_thread.thread_koid());

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kThread, m.type());
    EXPECT_EQ(123, m.GetThread().index);
    EXPECT_EQ(4, m.GetThread().process_thread.process_koid());
    EXPECT_EQ(5, m.GetThread().process_thread.thread_koid());

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kThread, r.type());
    EXPECT_EQ(123, r.GetThread().index);
    EXPECT_EQ(4, r.GetThread().process_thread.process_koid());
    EXPECT_EQ(5, r.GetThread().process_thread.thread_koid());

    EXPECT_STR_EQ("Thread(index: 123, 4/5)", r.ToString().c_str());
  }

  // event

  {
    fbl::Vector<trace::Argument> args;
    args.push_back(trace::Argument("arg1", trace::ArgumentValue::MakeInt32(11)));
    args.push_back(trace::Argument("arg2", trace::ArgumentValue::MakeDouble(-3.14)));

    trace::Record r(trace::Record::Event{123, trace::ProcessThread(4, 5), "category", "name",
                                         std::move(args),
                                         trace::EventData(trace::EventData::AsyncBegin{678})});
    EXPECT_EQ(trace::RecordType::kEvent, r.type());
    EXPECT_EQ(trace::EventType::kAsyncBegin, r.GetEvent().type());
    EXPECT_EQ(123, r.GetEvent().timestamp);
    EXPECT_EQ(4, r.GetEvent().process_thread.process_koid());
    EXPECT_EQ(5, r.GetEvent().process_thread.thread_koid());
    EXPECT_TRUE(r.GetEvent().category == "category");
    EXPECT_TRUE(r.GetEvent().name == "name");
    EXPECT_EQ(678, r.GetEvent().data.GetAsyncBegin().id);
    EXPECT_EQ(2, r.GetEvent().arguments.size());
    EXPECT_TRUE(r.GetEvent().arguments[0].name() == "arg1");
    EXPECT_EQ(11, r.GetEvent().arguments[0].value().GetInt32());
    EXPECT_TRUE(r.GetEvent().arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, r.GetEvent().arguments[1].value().GetDouble());

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kEvent, m.type());
    EXPECT_EQ(trace::EventType::kAsyncBegin, m.GetEvent().type());
    EXPECT_EQ(123, m.GetEvent().timestamp);
    EXPECT_EQ(4, m.GetEvent().process_thread.process_koid());
    EXPECT_EQ(5, m.GetEvent().process_thread.thread_koid());
    EXPECT_TRUE(m.GetEvent().category == "category");
    EXPECT_TRUE(m.GetEvent().name == "name");
    EXPECT_EQ(678, m.GetEvent().data.GetAsyncBegin().id);
    EXPECT_EQ(2, m.GetEvent().arguments.size());
    EXPECT_TRUE(m.GetEvent().arguments[0].name() == "arg1");
    EXPECT_EQ(11, m.GetEvent().arguments[0].value().GetInt32());
    EXPECT_TRUE(m.GetEvent().arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, m.GetEvent().arguments[1].value().GetDouble());

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kEvent, r.type());
    EXPECT_EQ(trace::EventType::kAsyncBegin, r.GetEvent().type());
    EXPECT_EQ(123, r.GetEvent().timestamp);
    EXPECT_EQ(4, r.GetEvent().process_thread.process_koid());
    EXPECT_EQ(5, r.GetEvent().process_thread.thread_koid());
    EXPECT_TRUE(r.GetEvent().category == "category");
    EXPECT_TRUE(r.GetEvent().name == "name");
    EXPECT_EQ(678, r.GetEvent().data.GetAsyncBegin().id);
    EXPECT_EQ(2, r.GetEvent().arguments.size());
    EXPECT_TRUE(r.GetEvent().arguments[0].name() == "arg1");
    EXPECT_EQ(11, r.GetEvent().arguments[0].value().GetInt32());
    EXPECT_TRUE(r.GetEvent().arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, r.GetEvent().arguments[1].value().GetDouble());

    EXPECT_STR_EQ(
        "Event(ts: 123, pt: 4/5, category: \"category\", name: \"name\", "
        "AsyncBegin(id: 678), {arg1: int32(11), arg2: double(-3.140000)})",
        r.ToString().c_str());
  }

  // blobs

  {
    const char name[] = "name";
    const char blob[] = "abc";
    const char preview[] = "<61 62 63 00>";
    trace::Record r(trace::Record::Blob{TRACE_BLOB_TYPE_DATA, "name", blob, sizeof(blob)});
    EXPECT_EQ(trace::RecordType::kBlob, r.type());
    EXPECT_EQ(TRACE_BLOB_TYPE_DATA, r.GetBlob().type);
    EXPECT_EQ(sizeof(blob), r.GetBlob().blob_size);
    EXPECT_STR_EQ(blob, reinterpret_cast<const char*>(r.GetBlob().blob));

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kBlob, m.type());
    EXPECT_EQ(TRACE_BLOB_TYPE_DATA, m.GetBlob().type);
    EXPECT_EQ(sizeof(blob), m.GetBlob().blob_size);
    EXPECT_STR_EQ(blob, reinterpret_cast<const char*>(m.GetBlob().blob));

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kBlob, r.type());
    EXPECT_EQ(TRACE_BLOB_TYPE_DATA, r.GetBlob().type);
    EXPECT_EQ(sizeof(blob), r.GetBlob().blob_size);
    EXPECT_STR_EQ(blob, reinterpret_cast<const char*>(r.GetBlob().blob));

    auto expected =
        fbl::StringPrintf("Blob(name: %s, size: %zu, preview: %s)", name, sizeof(blob), preview);
    EXPECT_STR_EQ(expected.c_str(), r.ToString().c_str());
  }

  // kernel object

  {
    fbl::Vector<trace::Argument> args;
    args.push_back(trace::Argument("arg1", trace::ArgumentValue::MakeInt32(11)));
    args.push_back(trace::Argument("arg2", trace::ArgumentValue::MakeDouble(-3.14)));

    trace::Record r(trace::Record::KernelObject{123, ZX_OBJ_TYPE_VMO, "name", std::move(args)});
    EXPECT_EQ(trace::RecordType::kKernelObject, r.type());
    EXPECT_EQ(123, r.GetKernelObject().koid);
    EXPECT_EQ(ZX_OBJ_TYPE_VMO, r.GetKernelObject().object_type);
    EXPECT_TRUE(r.GetKernelObject().name == "name");
    EXPECT_EQ(2, r.GetKernelObject().arguments.size());
    EXPECT_TRUE(r.GetKernelObject().arguments[0].name() == "arg1");
    EXPECT_EQ(11, r.GetKernelObject().arguments[0].value().GetInt32());
    EXPECT_TRUE(r.GetKernelObject().arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, r.GetKernelObject().arguments[1].value().GetDouble());

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kKernelObject, m.type());
    EXPECT_EQ(123, m.GetKernelObject().koid);
    EXPECT_EQ(ZX_OBJ_TYPE_VMO, m.GetKernelObject().object_type);
    EXPECT_TRUE(m.GetKernelObject().name == "name");
    EXPECT_EQ(2, m.GetKernelObject().arguments.size());
    EXPECT_TRUE(m.GetKernelObject().arguments[0].name() == "arg1");
    EXPECT_EQ(11, m.GetKernelObject().arguments[0].value().GetInt32());
    EXPECT_TRUE(m.GetKernelObject().arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, m.GetKernelObject().arguments[1].value().GetDouble());

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kKernelObject, r.type());
    EXPECT_EQ(123, r.GetKernelObject().koid);
    EXPECT_EQ(ZX_OBJ_TYPE_VMO, r.GetKernelObject().object_type);
    EXPECT_TRUE(r.GetKernelObject().name == "name");
    EXPECT_EQ(2, r.GetKernelObject().arguments.size());
    EXPECT_TRUE(r.GetKernelObject().arguments[0].name() == "arg1");
    EXPECT_EQ(11, r.GetKernelObject().arguments[0].value().GetInt32());
    EXPECT_TRUE(r.GetKernelObject().arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, r.GetKernelObject().arguments[1].value().GetDouble());

    EXPECT_STR_EQ(
        "KernelObject(koid: 123, type: vmo, name: \"name\", "
        "{arg1: int32(11), arg2: double(-3.140000)})",
        r.ToString().c_str());
  }

  // context switch

  {
    trace::Record r(trace::Record::ContextSwitch{123, 4, trace::ThreadState::kSuspended,
                                                 trace::ProcessThread(5, 6),
                                                 trace::ProcessThread(7, 8), 9, 10});
    EXPECT_EQ(trace::RecordType::kContextSwitch, r.type());
    EXPECT_EQ(123, r.GetContextSwitch().timestamp);
    EXPECT_EQ(4, r.GetContextSwitch().cpu_number);
    EXPECT_EQ(trace::ThreadState::kSuspended, r.GetContextSwitch().outgoing_thread_state);
    EXPECT_EQ(5, r.GetContextSwitch().outgoing_thread.process_koid());
    EXPECT_EQ(6, r.GetContextSwitch().outgoing_thread.thread_koid());
    EXPECT_EQ(7, r.GetContextSwitch().incoming_thread.process_koid());
    EXPECT_EQ(8, r.GetContextSwitch().incoming_thread.thread_koid());
    EXPECT_EQ(9, r.GetContextSwitch().outgoing_thread_priority);
    EXPECT_EQ(10, r.GetContextSwitch().incoming_thread_priority);

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kContextSwitch, m.type());
    EXPECT_EQ(123, m.GetContextSwitch().timestamp);
    EXPECT_EQ(4, m.GetContextSwitch().cpu_number);
    EXPECT_EQ(trace::ThreadState::kSuspended, m.GetContextSwitch().outgoing_thread_state);
    EXPECT_EQ(5, m.GetContextSwitch().outgoing_thread.process_koid());
    EXPECT_EQ(6, m.GetContextSwitch().outgoing_thread.thread_koid());
    EXPECT_EQ(7, m.GetContextSwitch().incoming_thread.process_koid());
    EXPECT_EQ(8, m.GetContextSwitch().incoming_thread.thread_koid());
    EXPECT_EQ(9, m.GetContextSwitch().outgoing_thread_priority);
    EXPECT_EQ(10, m.GetContextSwitch().incoming_thread_priority);

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kContextSwitch, r.type());
    EXPECT_EQ(123, r.GetContextSwitch().timestamp);
    EXPECT_EQ(4, r.GetContextSwitch().cpu_number);
    EXPECT_EQ(trace::ThreadState::kSuspended, r.GetContextSwitch().outgoing_thread_state);
    EXPECT_EQ(5, r.GetContextSwitch().outgoing_thread.process_koid());
    EXPECT_EQ(6, r.GetContextSwitch().outgoing_thread.thread_koid());
    EXPECT_EQ(7, r.GetContextSwitch().incoming_thread.process_koid());
    EXPECT_EQ(8, r.GetContextSwitch().incoming_thread.thread_koid());
    EXPECT_EQ(9, r.GetContextSwitch().outgoing_thread_priority);
    EXPECT_EQ(10, r.GetContextSwitch().incoming_thread_priority);

    EXPECT_STR_EQ(
        "ContextSwitch(ts: 123, cpu: 4, os: suspended, opt: 5/6, ipt: 7/8, oprio: 9, iprio: 10)",
        r.ToString().c_str());
  }

  // log

  {
    trace::Record r(trace::Record::Log{123, trace::ProcessThread(4, 5), "log message"});
    EXPECT_EQ(trace::RecordType::kLog, r.type());
    EXPECT_EQ(123, r.GetLog().timestamp);
    EXPECT_EQ(4, r.GetLog().process_thread.process_koid());
    EXPECT_EQ(5, r.GetLog().process_thread.thread_koid());
    EXPECT_TRUE(r.GetLog().message == "log message");

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kLog, m.type());
    EXPECT_EQ(123, m.GetLog().timestamp);
    EXPECT_EQ(4, m.GetLog().process_thread.process_koid());
    EXPECT_EQ(5, m.GetLog().process_thread.thread_koid());
    EXPECT_TRUE(m.GetLog().message == "log message");

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kLog, r.type());
    EXPECT_EQ(123, r.GetLog().timestamp);
    EXPECT_EQ(4, r.GetLog().process_thread.process_koid());
    EXPECT_EQ(5, r.GetLog().process_thread.thread_koid());
    EXPECT_TRUE(r.GetLog().message == "log message");

    EXPECT_STR_EQ("Log(ts: 123, pt: 4/5, \"log message\")", r.ToString().c_str());
  }

  // large blob

  {
    using std::get;
    using Format = trace::LargeRecordData::BlobEvent;

    const char blob[] = "abc";
    const char preview[] = "<61 62 63 00>";

    fbl::Vector<trace::Argument> args;
    args.push_back(trace::Argument("arg1", trace::ArgumentValue::MakeInt32(11)));
    args.push_back(trace::Argument("arg2", trace::ArgumentValue::MakeDouble(-3.14)));

    trace::Record r(trace::Record::Large{Format{"category", "name", 123, trace::ProcessThread(4, 5),
                                                std::move(args), blob, sizeof(blob)}});
    EXPECT_EQ(trace::RecordType::kLargeRecord, r.type());
    EXPECT_EQ(trace::LargeRecordType::kBlob, r.GetLargeRecord().type());
    EXPECT_TRUE(fit::holds_alternative<Format>(r.GetLargeRecord().GetBlob()));
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).category == "category");
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).name == "name");
    EXPECT_EQ(123, get<Format>(r.GetLargeRecord().GetBlob()).timestamp);
    EXPECT_EQ(4, get<Format>(r.GetLargeRecord().GetBlob()).process_thread.process_koid());
    EXPECT_EQ(5, get<Format>(r.GetLargeRecord().GetBlob()).process_thread.thread_koid());
    EXPECT_EQ(2, get<Format>(r.GetLargeRecord().GetBlob()).arguments.size());
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).arguments[0].name() == "arg1");
    EXPECT_EQ(11, get<Format>(r.GetLargeRecord().GetBlob()).arguments[0].value().GetInt32());
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, get<Format>(r.GetLargeRecord().GetBlob()).arguments[1].value().GetDouble());
    EXPECT_EQ(sizeof(blob), get<Format>(r.GetLargeRecord().GetBlob()).blob_size);
    EXPECT_STR_EQ(blob,
                  reinterpret_cast<const char*>(get<Format>(r.GetLargeRecord().GetBlob()).blob));

    trace::Record m(std::move(r));
    EXPECT_EQ(trace::RecordType::kLargeRecord, m.type());
    EXPECT_EQ(trace::LargeRecordType::kBlob, m.GetLargeRecord().type());
    EXPECT_TRUE(fit::holds_alternative<Format>(m.GetLargeRecord().GetBlob()));
    EXPECT_TRUE(get<Format>(m.GetLargeRecord().GetBlob()).category == "category");
    EXPECT_TRUE(get<Format>(m.GetLargeRecord().GetBlob()).name == "name");
    EXPECT_EQ(123, get<Format>(m.GetLargeRecord().GetBlob()).timestamp);
    EXPECT_EQ(4, get<Format>(m.GetLargeRecord().GetBlob()).process_thread.process_koid());
    EXPECT_EQ(5, get<Format>(m.GetLargeRecord().GetBlob()).process_thread.thread_koid());
    EXPECT_EQ(2, get<Format>(m.GetLargeRecord().GetBlob()).arguments.size());
    EXPECT_TRUE(get<Format>(m.GetLargeRecord().GetBlob()).arguments[0].name() == "arg1");
    EXPECT_EQ(11, get<Format>(m.GetLargeRecord().GetBlob()).arguments[0].value().GetInt32());
    EXPECT_TRUE(get<Format>(m.GetLargeRecord().GetBlob()).arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, get<Format>(m.GetLargeRecord().GetBlob()).arguments[1].value().GetDouble());
    EXPECT_EQ(sizeof(blob), get<Format>(m.GetLargeRecord().GetBlob()).blob_size);
    EXPECT_STR_EQ(blob,
                  reinterpret_cast<const char*>(get<Format>(m.GetLargeRecord().GetBlob()).blob));

    r = std::move(m);
    EXPECT_EQ(trace::RecordType::kLargeRecord, r.type());
    EXPECT_EQ(trace::LargeRecordType::kBlob, r.GetLargeRecord().type());
    EXPECT_TRUE(fit::holds_alternative<Format>(r.GetLargeRecord().GetBlob()));
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).category == "category");
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).name == "name");
    EXPECT_EQ(123, get<Format>(r.GetLargeRecord().GetBlob()).timestamp);
    EXPECT_EQ(4, get<Format>(r.GetLargeRecord().GetBlob()).process_thread.process_koid());
    EXPECT_EQ(5, get<Format>(r.GetLargeRecord().GetBlob()).process_thread.thread_koid());
    EXPECT_EQ(2, get<Format>(r.GetLargeRecord().GetBlob()).arguments.size());
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).arguments[0].name() == "arg1");
    EXPECT_EQ(11, get<Format>(r.GetLargeRecord().GetBlob()).arguments[0].value().GetInt32());
    EXPECT_TRUE(get<Format>(r.GetLargeRecord().GetBlob()).arguments[1].name() == "arg2");
    EXPECT_EQ(-3.14, get<Format>(r.GetLargeRecord().GetBlob()).arguments[1].value().GetDouble());
    EXPECT_EQ(sizeof(blob), get<Format>(r.GetLargeRecord().GetBlob()).blob_size);
    EXPECT_STR_EQ(blob,
                  reinterpret_cast<const char*>(get<Format>(r.GetLargeRecord().GetBlob()).blob));

    auto expected = fbl::StringPrintf(
        "LargeRecord(Blob(format: blob_event, category: \"category\", name: \"name\", "
        "ts: 123, pt: 4/5, {arg1: int32(11), arg2: double(-3.140000)}, "
        "size: %zu, preview: %s))",
        sizeof(blob), preview);
    EXPECT_STR_EQ(expected.c_str(), r.ToString().c_str());
  }
}

}  // namespace
}  // namespace trace

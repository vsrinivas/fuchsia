// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/sysmem-make-tracking/make_tracking.h"

#include <inttypes.h>
#include <lib/fidl/llcpp/heap_allocator.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/event.h>

#include <fidl/types/test/llcpp/fidl.h>
#include <zxtest/zxtest.h>

using namespace ::llcpp::fidl::types::test;

TEST(MakeTracking, PrimitiveTypeExplicitWithoutArgs) {
  fidl::HeapAllocator allocator;
  fidl::tracking_ptr<uint32_t> x = sysmem::MakeTracking<uint32_t>(allocator);
  EXPECT_EQ(0u, *x);
}

TEST(MakeTracking, PrimitiveTypeExplicitWithArgs) {
  fidl::HeapAllocator allocator;
  fidl::tracking_ptr<uint32_t> x = sysmem::MakeTracking<uint32_t>(allocator, 5u);
  EXPECT_EQ(5u, *x);
}

TEST(MakeTracking, PrimitiveTypeImplicit) {
  fidl::HeapAllocator allocator;
  fidl::tracking_ptr<uint32_t> x = sysmem::MakeTracking(allocator, 5u);
  EXPECT_EQ(5u, *x);
}

TEST(MakeTracking, CopyableStructExplicitWithoutArgs) {
  fidl::HeapAllocator allocator;
  fidl::tracking_ptr<CopyableStruct> x = sysmem::MakeTracking<CopyableStruct>(allocator);
  EXPECT_EQ(0, x->x);
  x->x = 5;
  EXPECT_EQ(5, x->x);
}

TEST(MakeTracking, CopyableStructExplicitWithArgs) {
  fidl::HeapAllocator allocator;
  CopyableStruct from = {.x = 5};
  fidl::tracking_ptr<CopyableStruct> x = sysmem::MakeTracking<CopyableStruct>(allocator, from);
  EXPECT_EQ(5, x->x);
}

TEST(MakeTracking, CopyableStructImplicit) {
  fidl::HeapAllocator allocator;
  CopyableStruct from{5};
  fidl::tracking_ptr<CopyableStruct> x = sysmem::MakeTracking(allocator, from);
  EXPECT_EQ(5, x->x);
}

TEST(MakeTracking, MoveOnlyStructExplicitWihtoutArgs) {
  fidl::HeapAllocator allocator;
  zx::event e;
  ZX_ASSERT(ZX_OK == zx::event::create(0, &e));
  zx::handle h(e.release());
  zx_handle_t h_value = h.get();
  fidl::tracking_ptr<MoveOnlyStruct> x = sysmem::MakeTracking<MoveOnlyStruct>(allocator);
  x->h = std::move(h);
  EXPECT_EQ(h_value, x->h.get());
}

TEST(MakeTracking, MoveOnlyStructExplicitWithArgs) {
  fidl::HeapAllocator allocator;
  zx::event e;
  ZX_ASSERT(ZX_OK == zx::event::create(0, &e));
  zx::handle h(e.release());
  zx_handle_t h_value = h.get();
  MoveOnlyStruct s;
  s.h = std::move(h);
  fidl::tracking_ptr<MoveOnlyStruct> x =
      sysmem::MakeTracking<MoveOnlyStruct>(allocator, std::move(s));
  EXPECT_EQ(h_value, x->h.get());
}

TEST(MakeTracking, MoveOnlyStructImplicit) {
  fidl::HeapAllocator allocator;
  zx::event e;
  ZX_ASSERT(ZX_OK == zx::event::create(0, &e));
  zx::handle h(e.release());
  zx_handle_t h_value = h.get();
  MoveOnlyStruct s;
  s.h = std::move(h);
  fidl::tracking_ptr<MoveOnlyStruct> x = sysmem::MakeTracking(allocator, std::move(s));
  EXPECT_EQ(h_value, x->h.get());
}

TEST(MakeTracking, TableExplititWithoutArgs) {
  fidl::HeapAllocator allocator;
  TableWithSubTables::Builder b = allocator.make_table_builder<TableWithSubTables>();
  b.set_t(sysmem::MakeTracking<SampleTable>(allocator));
  EXPECT_TRUE(b.has_t());
  EXPECT_FALSE(b.t().has_x());
  b.get_builder_t().set_x(sysmem::MakeTracking(allocator, static_cast<uint8_t>(5)));
  EXPECT_TRUE(b.t().has_x());
  EXPECT_EQ(5, b.t().x());
}

TEST(MakeTracking, TableExplicitWithArgs) {
  fidl::HeapAllocator allocator;
  SampleTable from = allocator.make_table_builder<SampleTable>()
                         .set_x(sysmem::MakeTracking(allocator, static_cast<uint8_t>(5)))
                         .build();
  fidl::tracking_ptr<SampleTable> x = sysmem::MakeTracking<SampleTable>(allocator, std::move(from));
  EXPECT_EQ(5, x->x());
}

TEST(MakeTracking, TableImplicit) {
  fidl::HeapAllocator allocator;
  SampleTable from = allocator.make_table_builder<SampleTable>()
                         .set_x(sysmem::MakeTracking(allocator, static_cast<uint8_t>(5)))
                         .build();
  fidl::tracking_ptr<SampleTable> x = sysmem::MakeTracking(allocator, std::move(from));
  EXPECT_EQ(5, x->x());
}

TEST(MakeTracking, BuilderExplicit) {
  fidl::HeapAllocator allocator;
  SampleTable::Builder from = allocator.make_table_builder<SampleTable>().set_x(
      sysmem::MakeTracking(allocator, static_cast<uint8_t>(5)));
  fidl::tracking_ptr<SampleTable> x = sysmem::MakeTracking<SampleTable>(allocator, std::move(from));
  EXPECT_EQ(5, x->x());
}

TEST(MakeTracking, BuilderImplicit) {
  fidl::HeapAllocator allocator;
  SampleTable::Builder from = allocator.make_table_builder<SampleTable>().set_x(
      sysmem::MakeTracking(allocator, static_cast<uint8_t>(5)));
  fidl::tracking_ptr<SampleTable> x = sysmem::MakeTracking(allocator, std::move(from));
  EXPECT_EQ(5, x->x());
}

TEST(MakeTracking, VectorViewOfPrimitiveExplicit) {
  fidl::HeapAllocator allocator;
  constexpr size_t kCount = 30;
  fidl::VectorView<uint32_t> v = allocator.make_vec<uint32_t>(kCount);
  v[0] = 12;
  fidl::tracking_ptr<fidl::VectorView<uint32_t>> tv =
      sysmem::MakeTracking<fidl::VectorView<uint32_t>>(allocator, std::move(v));
  EXPECT_EQ(12u, (*tv)[0]);
}

TEST(MakeTracking, VectorViewOfPrimitiveImplicit) {
  fidl::HeapAllocator allocator;
  constexpr size_t kCount = 30;
  fidl::VectorView<uint32_t> v = allocator.make_vec<uint32_t>(kCount);
  v[0] = 12;
  fidl::tracking_ptr<fidl::VectorView<uint32_t>> tv = sysmem::MakeTracking(allocator, std::move(v));
  EXPECT_EQ(12u, (*tv)[0]);
}

TEST(MakeTracking, VectorViewOfTableExplicit) {
  fidl::HeapAllocator allocator;
  constexpr size_t kCount = 30;
  fidl::VectorView<SampleTable> v = allocator.make_vec<SampleTable>(kCount);
  v[0] = allocator.make_table_builder<SampleTable>()
             .set_x(sysmem::MakeTracking(allocator, static_cast<uint8_t>(12)))
             .build();
  fidl::tracking_ptr<fidl::VectorView<SampleTable>> tv =
      sysmem::MakeTracking<fidl::VectorView<SampleTable>>(allocator, std::move(v));
  EXPECT_EQ(12, (*tv)[0].x());
}

TEST(MakeTracking, VectorViewOfTableImplicit) {
  fidl::HeapAllocator allocator;
  constexpr size_t kCount = 30;
  fidl::VectorView<SampleTable> v = allocator.make_vec<SampleTable>(kCount);
  v[0] = allocator.make_table_builder<SampleTable>()
             .set_x(sysmem::MakeTracking(allocator, static_cast<uint8_t>(12)))
             .build();
  fidl::tracking_ptr<fidl::VectorView<SampleTable>> tv =
      sysmem::MakeTracking(allocator, std::move(v));
  EXPECT_EQ(12, (*tv)[0].x());
}

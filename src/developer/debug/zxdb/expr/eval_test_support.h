// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_TEST_SUPPORT_H_

#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"

namespace zxdb {

// Several casting tests test base/derived class conversions. This structure contains the elaborate
// setup for these related cases. It declares:
//
//   struct Base1 {
//     int16_t b = kB1Value;
//   };
//
//   struct Base2 {
//     int32_t b = kB2Value;
//   };
//
//   struct Derived : public Base1, public Base2 {
//     int32_t d = kDValue;
//   };
//
// Derived derived_value;
//
// Derived* derived_ptr_value = &derived_value;
// Base1* base1_ptr_value = &derived_value;
// Base2* base2_ptr_value = &derived_value;
struct DerivedClassTestSetup {
  DerivedClassTestSetup();

  fxl::RefPtr<Collection> base1_type;
  fxl::RefPtr<Collection> base2_type;
  fxl::RefPtr<Collection> derived_type;

  fxl::RefPtr<ModifiedType> base1_ptr_type;    // Base1*
  fxl::RefPtr<ModifiedType> base2_ptr_type;    // Base2*
  fxl::RefPtr<ModifiedType> derived_ptr_type;  // Derived*

  fxl::RefPtr<ModifiedType> base1_ref_type;    // Base1&
  fxl::RefPtr<ModifiedType> base2_ref_type;    // Base2&
  fxl::RefPtr<ModifiedType> derived_ref_type;  // Derived&

  ExprValue base1_value;  // References inside of derived.
  ExprValue base2_value;  // References inside of derived.
  ExprValue derived_value;

  ExprValue base1_ptr_value;    // Base1* base1_ptr_value = &base1_value;
  ExprValue base2_ptr_value;    // Base2* base2_ptr_value = &base2_value;
  ExprValue derived_ptr_value;  // Derived* derived_ptr_value = &derived_value;

  ExprValue base1_ref_value;    // Base1& base1_ref_value = base1_value;
  ExprValue base2_ref_value;    // Base1& base2_ref_value = base2_value;
  ExprValue derived_ref_value;  // Derived& derived_ref_value = derived_value;

  uint64_t kBase1Offset = 0;  // Offset of Base1 in Derived.
  uint64_t kBase2Offset = 2;  // Offset of Base2 in Derived.

  uint64_t kDerivedAddr = 0x3000;                     // &derived_value
  uint64_t kBase1Addr = kDerivedAddr + kBase1Offset;  // &base1_value
  uint64_t kBase2Addr = kDerivedAddr + kBase2Offset;  // &base2_value
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_TEST_SUPPORT_H_

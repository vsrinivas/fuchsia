// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_TEST_EVAL_CONTEXT_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_TEST_EVAL_CONTEXT_IMPL_H_

#include "src/developer/debug/zxdb/expr/eval_context_impl.h"

namespace zxdb {

// This class provides a way to control the features not provided by EvalContextImpl.
//
// Some settings are provided at a higher layer for the EvalContext. In production this is done in
// the client layer to hook it to the settings system.
//
// This class provides a way to explicitly set these settings without invoking the client layer. It
// is for testing, but otherwise it is the full EvalContextImpl.
class TestEvalContextImpl : public EvalContextImpl {
 public:
  // Construct via fxl::MakeRefCounted().

  void set_should_promote_to_derived(bool should_promote) { should_promote_ = should_promote; }

  // EvalContext overrides.
  bool ShouldPromoteToDerived() const override { return should_promote_; }

 private:
  FRIEND_MAKE_REF_COUNTED(TestEvalContextImpl);
  FRIEND_REF_COUNTED_THREAD_SAFE(TestEvalContextImpl);

  TestEvalContextImpl(std::shared_ptr<Abi> abi, fxl::WeakPtr<const ProcessSymbols> process_symbols,
                      fxl::RefPtr<SymbolDataProvider> data_provider, ExprLanguage language)
      : EvalContextImpl(std::move(abi), std::move(process_symbols), std::move(data_provider),
                        language) {}

  bool should_promote_ = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_TEST_EVAL_CONTEXT_IMPL_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class OutputBuffer;
class SymbolContext;
class SymbolDataProvider;
class SymbolVariableResolver;
class Value;
class Variable;

struct FormatValueOptions {
  enum class NumFormat { kDefault, kUnsigned, kSigned, kHex, kChar };

  // Format to apply to numeric types.
  NumFormat num_format = NumFormat::kDefault;
};

// Formats the given expression value to the output buffer. The variant that
// takes an Err will do an error check before printing the value, and will
// output the appropriate error message instead if there is one. It will modify
// the error messaage to be appropriate as a replacement for a value.
// output the appropriate error message instead if there is one.
void FormatExprValue(const ExprValue& value, const FormatValueOptions& options,
                     OutputBuffer* out);
void FormatExprValue(const Err& err, const ExprValue& value,
                     const FormatValueOptions& options, OutputBuffer* out);

// Sometimes one will want to output multiple values, maybe with other stuff
// interleaved, in one stream. Since formatting is asynchronous this can be
// tricky. This class manages a set of output operations interleaved with
// asynchronous formatted values.
//
// When all requested asynchronous formatting is complete, the callback will
// be issued with the concatenated result.
//
// When all output is done being appended, call Complete() to schedule the
// final callback.
class ValueFormatHelper : public fxl::RefCountedThreadSafe<ValueFormatHelper> {
 public:
  using Callback = std::function<void(OutputBuffer)>;

  // Construct with fxl::MakeRefCounted<ValueFormatHelper>().

  // The data provider normally comes from the frame where you want to evaluate
  // the variable in.
  void AppendVariable(const SymbolContext& symbol_context,
                      fxl::RefPtr<SymbolDataProvider> data_provider,
                      const Variable* var, const FormatValueOptions& options);

  // Writes "<name> = <value>" to the buffer.
  void AppendVariableWithName(const SymbolContext& symbol_context,
                              fxl::RefPtr<SymbolDataProvider> data_provider,
                              const Variable* var,
                              const FormatValueOptions& options);

  void Append(std::string str);
  void Append(OutputBuffer out);

  // Call after all data has been appended.
  //
  // This needs to be a sepatate call since not all output is asynchronous, and
  // we don't want to call a callback before everything is complete, or not at
  // all.
  void Complete(Callback callback);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ValueFormatHelper);
  FRIEND_MAKE_REF_COUNTED(ValueFormatHelper);

  ValueFormatHelper();
  ~ValueFormatHelper();

  // Issues the pending callback if necessary. The callback may delete |this|
  // so the caller should immediately return after calling.
  void CheckPendingResolution();

  Callback complete_callback_;
  std::vector<OutputBuffer> buffers_;

  std::vector<std::unique_ptr<SymbolVariableResolver>> resolvers_;

  int pending_resolution_ = 0;
};

}  // namespace zxdb

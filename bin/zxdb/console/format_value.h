// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <vector>

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

// TODO(brettw) remove this variant.
void FormatValue(const Value* value, OutputBuffer* out);

// Formats the given expression value to the output buffer. The variant that
// takes an Err will do an error check before printing the value, and will
// output the appropriate error message instead if there is one. It will modify
// the error messaage to be appropriate as a replacement for a value.
void FormatExprValue(const ExprValue& value, OutputBuffer* out);
void FormatExprValue(const Err& err, const ExprValue& value, OutputBuffer* out);

void FormatVariable(fxl::RefPtr<SymbolDataProvider> data_provider,
                    const Variable* var,
                    std::function<void(OutputBuffer)> result);

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
class ValueFormatHelper {
 public:
  using Callback = std::function<void(OutputBuffer)>;

  ValueFormatHelper();
  ~ValueFormatHelper();

  // The data provider normally comes from the frame where you want to evaluate
  // the variable in.
  void AppendVariable(const SymbolContext& symbol_context,
                      fxl::RefPtr<SymbolDataProvider> data_provider,
                      const Variable* var);

  // Writes "<name> = <value>" to the buffer.
  void AppendVariableWithName(const SymbolContext& symbol_context,
                              fxl::RefPtr<SymbolDataProvider> data_provider,
                              const Variable* var);

  void Append(OutputBuffer out);

  // Call after all data has been appended.
  //
  // This needs to be a sepatate call since not all output is asynchronous, and
  // we don't want to call a callback before everything is complete, or not at
  // all.
  void Complete(Callback callback);

 private:
  void CheckPendingResolution();

  Callback complete_callback_;
  std::vector<OutputBuffer> buffers_;

  std::vector<std::unique_ptr<SymbolVariableResolver>> resolvers_;

  int pending_resolution_ = 0;
};

}  // namespace zxdb

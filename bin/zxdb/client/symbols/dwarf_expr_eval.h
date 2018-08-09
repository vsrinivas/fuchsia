// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace llvm {
class DataExtractor;
}  // namespace llvm

namespace zxdb {

class SymbolDataProvider;

// This class evaluates DWARF expressions. These expressions are used to encode
// the locations of variables and a few other nontrivial lookups.
//
// This class is complicated by supporting asynchronous interactions with the
// debugged program. This means that accessing register and memory data (which
// may be required to evaluate the expression) may be asynchronous.
//
//  eval_ = std::make_unique<DwarfExprEval>();
//  eval_.eval(..., [](DwarfExprEval* eval, const Err& err) {
//    if (err.has_error()) {
//      // Handle error.
//    } else {
//      ... use eval->GetResult() ...
//    }
//  });
class DwarfExprEval {
 public:
  // Type of completion from a call. Async completion will happen in a callback
  // in the future.
  enum class Completion { kSync, kAsync };

  // A DWARF expression can compute either the address of the desired object in
  // the debugged programs address space, or it can compute the actual value of
  // the object (because it may not expst in memory).
  enum class ResultType { kPointer, kValue };

  // Storage for opcode data.
  using Expression = std::vector<uint8_t>;

  using CompletionCallback =
      std::function<void(DwarfExprEval* eval, const Err& err)>;

  DwarfExprEval();
  ~DwarfExprEval();

  // A complete expression has finished executing but may or may not have had
  // an error. A successful expression indicates execution is complete and
  // there is a valid result to read.
  bool is_complete() const { return is_complete_; }
  bool is_success() const { return is_success_; }

  // Valid when is_success(), this indicates how to interpret the value from
  // GetResult().
  ResultType GetResultType() const;

  // Valid when is_success(), this returns the result of evaluating the
  // expression. The meaning will be dependent on the context of the expression
  // being evaluated.
  uint64_t GetResult() const;

  // This will take a reference to the SymbolDataProvider until the computation
  // is complete.
  //
  // The return value will indicate if the request completed synchronously. In
  // synchronous completion the callback will have been called reentrantly from
  // within the stack of this function. This does not indicate success as it
  // could suceed or fail both synchronously and asynchronously.
  Completion Eval(fxl::RefPtr<SymbolDataProvider> data_provider,
                  Expression expr, CompletionCallback cb);

 private:
  // Evaluates the next phases of the expression until an asynchronous operation
  // is required.
  void ContinueEval();

  // Evaluates a single operation.
  Completion EvalOneOp();

  // Adds a register's contents + an offset to the stack. Use 0 for the offset
  // to get the raw register value.
  Completion PushRegisterWithOffset(int dwarf_register_number, int64_t offset);

  // Pushes a value on the stack.
  void Push(uint64_t value);

  // These read constant data from the current index in the stream. The size of
  // the data is in byte_size, and the result will be extended to 64 bits
  // according to the type.
  //
  // They return true if the value was read, false if there wasn't enough data
  // (they will issue the error internally, the calling code should just return
  // on failure).
  bool ReadSigned(int byte_size, int64_t* output);
  bool ReadUnsigned(int byte_size, uint64_t* output);

  // Reads a signed or unsigned LEB constant from the stream. They return true
  // if the value was read, false if there wasn't enough data (they will issue
  // the error internally, the calling code should just return on failure).
  bool ReadLEBSigned(int64_t* output);
  bool ReadLEBUnsigned(uint64_t* output);

  void ReportError(const std::string& msg);
  void ReportStackUnderflow();
  void ReportUnimplementedOpcode(uint8_t op);

  // Executes the given unary operation with the top stack entry as the
  // parameter and pushes the result.
  Completion OpUnary(uint64_t (*op)(uint64_t));

  // Executes the given binary operation by popping the top two stack entries
  // as parameters (the first is the next-to-top, the second is the top) and
  // pushing the result on the stack.
  Completion OpBinary(uint64_t (*op)(uint64_t, uint64_t));

  // Operations. On call, the expr_index_ will index the byte following the
  // opcode, and on return expr_index_ will index the next instruction (any
  // parameters will be consumed).
  Completion OpBra();
  Completion OpBreg(uint8_t op);
  Completion OpDiv();
  Completion OpDrop();
  Completion OpDup();
  Completion OpRegx();
  Completion OpBregx();
  Completion OpMod();
  Completion OpOver();
  Completion OpPick();
  Completion OpPlusUconst();
  Completion OpPushSigned(int byte_count);
  Completion OpPushUnsigned(int byte_count);
  Completion OpPushLEBSigned();
  Completion OpPushLEBUnsigned();
  Completion OpRot();
  Completion OpSkip();
  Completion OpStackValue();
  Completion OpSwap();

  // Adjusts the instruction offset by the given amount, handling out-of-bounds
  // as appropriate. This is the backend for jumps and branches.
  void Skip(int64_t amount);

  fxl::RefPtr<SymbolDataProvider> data_provider_;

  // The expression. See also expr_index_.
  Expression expr_;

  // Index into expr_ of the next thing to read. This is a uint32_t to
  // intergrate with LLVM DataExtractor.
  uint32_t expr_index_ = 0;

  CompletionCallback completion_callback_;

  // Allocated on the heap to avoid exposing LLVM headers.
  std::unique_ptr<llvm::DataExtractor> data_extractor_;

  // The result type. Normally expressions compute pointers unless explicitly
  // tagged as a value.
  ResultType result_type_ = ResultType::kPointer;

  // Indicates that execution is complete. When this is true, the callback will
  // have been issued. A complete expression could have stopped on error or
  // success (see is_success_).
  bool is_complete_ = false;

  // Indicates that the expression is complete and that there is a result
  // value.
  bool is_success_ = false;

  std::vector<uint64_t> stack_;

  fxl::WeakPtrFactory<DwarfExprEval> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DwarfExprEval);
};

}  // namespace zxdb

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/expr/format_expr_value_options.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Collection;
class Enumeration;
class Err;
class ExprValue;
class Location;
class MemberPtr;
class OutputBuffer;
class SymbolContext;
class SymbolDataProvider;
class SymbolVariableResolver;
class Type;
class Value;
class Variable;

// Manages formatting of variables and ExprValues (the results of expressions).
// Since formatting is asynchronous this can be tricky. This class manages a
// set of output operations interleaved with synchronously and asynchronously
// formatted values.
//
// When all requested formatting is complete, the callback will be issued with
// the concatenated result.
//
// When all output is done being appended, call Complete() to schedule the
// final callback.
//
// In common usage the helper can actually be owned by the callback to keep it
// alive during processing and automatically delete it when done:
//
//   auto helper = fxl::MakeRefCounted<FormatValue>();
//   helper->Append(...);
//   // IMPORTANT: Do not move helper into this call or the RefPtr can get
//   // cleared before invoking the call!
//   helper->Complete([helper](OutputBuffer out) { UseIt(out); });
class FormatValue : public fxl::RefCountedThreadSafe<FormatValue> {
 public:
  // Abstract interface for looking up information about a process.
  class ProcessContext {
   public:
    virtual ~ProcessContext() = default;

    // Given an address in the process, returns the (symbolized if possible)
    // Location for that address.
    virtual Location GetLocationForAddress(uint64_t address) const = 0;
  };

  using Callback = std::function<void(OutputBuffer)>;

  // Construct with fxl::MakeRefCounted<FormatValue>().

  void AppendValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                   const ExprValue& value,
                   const FormatExprValueOptions& options);

  // The data provider normally comes from the frame where you want to evaluate
  // the variable in. This will prepend "<name> = " to the value of the
  // variable.
  void AppendVariable(const SymbolContext& symbol_context,
                      fxl::RefPtr<SymbolDataProvider> data_provider,
                      const Variable* var,
                      const FormatExprValueOptions& options);

  void Append(std::string str);
  void Append(OutputBuffer out);

  // Call after all data has been appended.
  //
  // This needs to be a separate call since not all output is asynchronous, and
  // we don't want to call a callback before everything is complete, or not at
  // all.
  void Complete(Callback callback);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(FormatValue);
  FRIEND_MAKE_REF_COUNTED(FormatValue);

  enum class NodeType { kGeneric, kVariable, kBaseClass };

  // Output is multilevel and each level can be asynchronous (a struct can
  // include another struct which can include an array, etc.).
  //
  // As we build up the formatted output, each composite type
  // (struct/class/array) adds a new node with its contents as children.
  // Asynchronous operations can fill in the buffers of these nodes, and when
  // all output is complete, the tree can be flattened to produce the final
  // result.
  //
  // REFACTORING IN PROGRESS...
  //
  // Originally OutputNode just held an OutputBuffer in a hierarchical way
  // that can be referenced externally as an OutputKey. Strings were filled in
  // by the value formatter as it went.
  //
  // We would like to move to a design where value formatting is split in two
  // parts: (1) computing a tree of values, (2) converting that tree to text.
  // The first part should be in the client or expr directory, while the text
  // formatting should be in the console directory.
  //
  // With this split, we can drive other types of UIs without duplicating the
  // complex type deduction and stringification logic. Even with the console
  // output only, this design would let us do smarter wrapping since the
  // complete result is known at text-generation time.
  //
  // This class will morph into the tree node. Currently it is a bit of a
  // hybrid where it can store a higher level concept (given by NodeType) with
  // an optional name, or it can just be random text.
  struct OutputNode {
    // Optional.
    std::string name;
    NodeType type = NodeType::kGeneric;

    OutputBuffer buffer;  // Only used when there are no children.

    // Used for sanity checking. This is set when waiting on async resolution
    // on a given node, and cleared when async resolution is complete. It
    // makes sure we don't miss or double-set anything.
    bool pending = false;

    // The children must be heap-allocated because the pointers (in the form of
    // an OutputKey) will be passed around across vector resizes.
    std::vector<std::unique_ptr<OutputNode>> child;
  };

  // Identifies an output buffer to write asynchronously to.
  //
  // This is actually an OutputNode*. The tricky thing is that the pointers
  // are all owned by the FormatValue class. If the class goes out of scope
  // the pointers will be invalidated, but they may still be referenced by
  // in-progress callbacks.
  //
  // To avoid the temptation to write to the OutputBuffer directly from these
  // callbacks, or to forget to check for completion, this type requires a
  // cast. OutputKeyComplete does the cast and checks whether all callbacks are
  // complete. By being on the object itself, it forces asynchronous callbacks
  // to resolve their weak back-pointers first.
  //
  // No code should ever case this other than the functions that manipulate the
  // keys (AppendToOutputKey, AsyncAppend, OutputKeyComplete).
  using OutputKey = intptr_t;

  explicit FormatValue(std::unique_ptr<ProcessContext> process_context);
  ~FormatValue();

  // Formats the given expression value to the output buffer. The variant that
  // takes an Err will do an error check before printing the value, and will
  // output the appropriate error message instead if there is one. It will
  // modify the error message to be appropriate as a replacement for a value.
  // output the appropriate error message instead if there is one.
  //
  // When set, suppress_type_printing will suppress the use of
  // options.always_show_types for this item only (but not nested items). This
  // is designed to be used when called recursively and the type has already
  // been printed.
  void FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const ExprValue& value,
                       const FormatExprValueOptions& options,
                       bool suppress_type_printing, OutputKey output_key);
  void FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const Err& err, const ExprValue& value,
                       const FormatExprValueOptions& options,
                       bool suppress_type_printing, OutputKey output_key);

  // Asynchronously formats the given type.
  //
  // The known_elt_count can be -1 if the array size is not statically known.
  void FormatCollection(fxl::RefPtr<SymbolDataProvider> data_provider,
                        const Collection* coll, const ExprValue& value,
                        const FormatExprValueOptions& options,
                        OutputKey output_key);

  // Checks array and string types and formats the value accordingly. Returns
  // true if it was an array or string type that was handled, false if it
  // was anything else.
  bool TryFormatArrayOrString(fxl::RefPtr<SymbolDataProvider> data_provider,
                              const Type* type, const ExprValue& value,
                              const FormatExprValueOptions& options,
                              OutputKey output_key);

  // Array and string format helpers.
  void FormatCharPointer(fxl::RefPtr<SymbolDataProvider> data_provider,
                         const Type* type, const ExprValue& value,
                         const FormatExprValueOptions& options,
                         OutputKey output_key);
  void FormatCharArray(const uint8_t* data, size_t length, bool truncated,
                       OutputKey output_key);
  void FormatArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                   const ExprValue& value, int elt_count,
                   const FormatExprValueOptions& options, OutputKey output_key);

  // Dispatcher for all numeric types. This handles formatting overrides.
  void FormatNumeric(const ExprValue& value,
                     const FormatExprValueOptions& options, OutputBuffer* out);

  // Simpler synchronous outputs.
  void FormatBoolean(const ExprValue& value, OutputBuffer* out);
  void FormatEnum(const ExprValue& value, const Enumeration* enum_type,
                  const FormatExprValueOptions& options, OutputBuffer* out);
  void FormatFloat(const ExprValue& value, OutputBuffer* out);
  void FormatSignedInt(const ExprValue& value, OutputBuffer* out);
  void FormatUnsignedInt(const ExprValue& value,
                         const FormatExprValueOptions& options,
                         OutputBuffer* out);
  void FormatChar(const ExprValue& value, OutputBuffer* out);
  void FormatPointer(const ExprValue& value,
                     const FormatExprValueOptions& options, OutputBuffer* out);
  void FormatReference(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const ExprValue& value,
                       const FormatExprValueOptions& options,
                       OutputKey output_key);
  void FormatFunctionPointer(const ExprValue& value,
                             const FormatExprValueOptions& options,
                             OutputBuffer* out);
  void FormatMemberPtr(const ExprValue& value, const MemberPtr* type,
                       const FormatExprValueOptions& options,
                       OutputBuffer* out);

  OutputKey GetRootOutputKey();

  // Appends a child node to the output key without opening an async
  // transaction.
  void AppendToOutputKey(OutputKey output_key, OutputBuffer buffer);

  // An asynchronous version of AppendToOutputKey. The returned key is a
  // sub-key for use in later appending. Call OutputKeyComplete when this is
  // done.
  OutputKey AsyncAppend(OutputKey parent);
  OutputKey AsyncAppend(NodeType type, std::string name, OutputKey parent);

  // Marks the given output key complete. The variant that takes an output
  // buffer is a shorthand for appending the contents and marking it complete.
  // This will check for completion and issue the callback if everything has
  // been resolved.
  void OutputKeyComplete(OutputKey key);
  void OutputKeyComplete(OutputKey key, OutputBuffer contents);

  // Issues the pending callback if necessary. The callback may delete |this|
  // so the caller should immediately return after calling.
  void CheckPendingResolution();

  // Recursively walks the OutputNode tree to produce the final output in
  // the given output buffer. The sources are moved from so this is
  // destructive.
  void RecursiveCollectOutput(OutputNode* node, OutputBuffer* out);

  std::unique_ptr<ProcessContext> process_context_;
  Callback complete_callback_;
  std::vector<OutputBuffer> buffers_;

  std::vector<std::unique_ptr<SymbolVariableResolver>> resolvers_;

  // The root of the output.
  OutputNode root_;

  int pending_resolution_ = 0;

  fxl::WeakPtrFactory<FormatValue> weak_factory_;
};

}  // namespace zxdb

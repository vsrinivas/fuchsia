// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_

#include <optional>

#include "lib/fit/defer.h"
#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format_node.h"

namespace zxdb {

class EvalContext;
struct FormatOptions;

// Fills the value() of a FormatNode based on its expression. This does not update the description
// based on the new value. The node can be in any state and this function will fill the value if
// possible.
//
// The callback will be called on completion. This may occur synchronously (within the stack of this
// function call) or in the future. If it happens in the future, the node will be referenced by weak
// pointer so the caller does not have to worry about lifetime issues.
//
// The callback will always be issued, even if the node is destroyed. Callers should keep a weak
// pointer to the node if they do not control its lifetime.
//
// TODO(brettw) should this be a member of FormatNode?
void FillFormatNodeValue(FormatNode* node, const fxl::RefPtr<EvalContext>& context,
                         fit::deferred_callback cb);

// Fills the description and children of a FormatNode based on the current value().
//
// The callback will be called on completion. This may occur synchronously (within the stack of this
// function call) or in the future. If it happens in the future, the node will be referenced by weak
// pointer so the caller does not have to worry about lifetime issues.
//
// The callback will always be issued, even if the node is destroyed. Callers should keep a weak
// pointer to the node if they do not control its lifetime.
void FillFormatNodeDescription(FormatNode* node, const FormatOptions& options,
                               const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb);

// Formatter for numbers. This assumes the type of the value in the given node has already been
// determined to be numeric. This may also be called as a fallback for things like enums.
void FormatNumericNode(FormatNode* node, const FormatOptions& options);

// Formatters for strings. These are public so they can be shared by the pretty-printers.
//
// The "char pointer" variant can take a known string length or not. If one is not given, the
// function will look for a null-terminated string.
//
// TODO(brettw) we probably want a more general way for pretty-printers to call into our default
// code for handling certain types.
void FormatCharArrayNode(FormatNode* node, fxl::RefPtr<Type> char_type, const uint8_t* data,
                         size_t length, bool length_was_known, bool truncated);
void FormatCharPointerNode(FormatNode* node, uint64_t ptr, const Type* char_type,
                           std::optional<uint32_t> length, const FormatOptions& options,
                           const fxl::RefPtr<EvalContext>& eval_context, fit::deferred_callback cb);

// Formats an array with a known length. This is for non-char arrays (which are special-cased in
// FormatCharArrayNode).
//
// The value is given rather than being extracted from the node so it can be different. It can be
// either an Array symbol type or a pointer.
void FormatArrayNode(FormatNode* node, const ExprValue& value, int elt_count,
                     const FormatOptions& options, const fxl::RefPtr<EvalContext>& eval_context,
                     fit::deferred_callback cb);

// Formats a node for a pointer. This function is synchronous.
//
// The value is given rather than taken from the node to support pretty-printing uses.
void FormatPointerNode(FormatNode* node, const ExprValue& value, const FormatOptions& options);

// Fills a format node for something that holds another value. This would be used for things like
// atomics, optionals, and iterators where there's some indirection.
//
// The node will be given the description, and it will have one child with the given name and value.
// For convenience, this allows an value, an error, or a lambda for computing the value.
void FormatWrapper(FormatNode* node, const std::string& description, const std::string& prefix,
                   const std::string& suffix, const std::string& contained_name,
                   ErrOrValue contained_value);
void FormatWrapper(FormatNode* node, const std::string& description, const std::string& prefix,
                   const std::string& suffix, const std::string& contained_name,
                   FormatNode::GetProgramaticValue value_getter);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_

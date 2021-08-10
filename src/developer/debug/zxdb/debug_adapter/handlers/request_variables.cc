// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <dap/protocol.h>
#include <dap/session.h>
#include <dap/types.h>

#include "src/developer/debug/shared/register_info.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/debug_adapter/context.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_scopes.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/value.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

// Class to share relevant information with all variable evaluate callbacks.
// Number of evaluations should be specified during the construction of the class. Upon completion
// of each evaluation a FormatNode is expected to be passed to `OnComplete()`. Using this FormatNode
// the variable response is updated. Once all pending evaluations are complete, response is sent to
// the client through `callback`.
class VariableResponseContext : public fxl::RefCountedThreadSafe<VariableResponseContext> {
 public:
  explicit VariableResponseContext(
      DebugAdapterContext* context, dap::VariablesRequest request,
      std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback,
      fxl::RefPtr<EvalContext> eval_context, int pending_evals)
      : context(context),
        request(std::move(request)),
        callback(std::move(callback)),
        eval_context(std::move(eval_context)),
        pending_evals(pending_evals) {}

  ~VariableResponseContext() {
    // Check that all evaluations are complete.
    FX_CHECK(!pending_evals);
  }

  void OnComplete(std::unique_ptr<FormatNode> node) {
    FX_CHECK(node);
    auto var = VariableForNode(std::move(node));
    response.variables.push_back(var);
    EvalComplete();
  }

  void OnComplete(const fxl::WeakPtr<FormatNode>& node) {
    if (node) {
      auto var = VariableForNode(node);
      response.variables.push_back(var);
    }
    EvalComplete();
  }

  DebugAdapterContext* context;
  dap::VariablesRequest request;
  std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback;
  fxl::RefPtr<EvalContext> eval_context;
  int pending_evals = 0;
  dap::VariablesResponse response;

 private:
  template <typename T>
  dap::Variable VariableForNode(T node) {
    dap::Variable var;
    var.name = node->name();
    if (node->state() != FormatNode::kDescribed) {
      // Value not yet available.
      var.value = "...";
    } else if (node->err().has_error()) {
      // Write the error.
      var.value = "<" + node->err().msg() + ">";
    } else {
      // Normal formatting.
      var.value = node->description();
    }
    var.type = node->type();
    var.variablesReference = ChildReference(std::move(node));
    return var;
  }

  int ChildReference(std::unique_ptr<FormatNode> node) const {
    if (!node->children().empty()) {
      VariablesRecord* record = context->VariablesRecordForID(request.variablesReference);
      FX_CHECK(record);
      return context->IdForVariables(record->frame_id, VariablesType::kChildVariable,
                                     std::move(node), nullptr);
    }
    return 0;
  }

  int ChildReference(const fxl::WeakPtr<FormatNode>& node) const {
    if (!node->children().empty()) {
      VariablesRecord* record = context->VariablesRecordForID(request.variablesReference);
      FX_CHECK(record);
      return context->IdForVariables(record->frame_id, VariablesType::kChildVariable, nullptr,
                                     node);
    }
    return 0;
  }

  void EvalComplete() {
    FX_CHECK(pending_evals);
    // This would not cause TOCTOU as this is called on a single thread.
    pending_evals--;
    if (pending_evals == 0) {
      callback(response);
    }
  }
};

// Evaluate variable value and fill its descriptions.
Err PopulateVariableValues(
    Frame* frame, const dap::VariablesRequest& req, DebugAdapterContext* ctx,
    std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback,
    std::map<std::string, fxl::RefPtr<Variable>>& vars) {
  int start_index = req.start.value(0);
  int end_index = vars.size();
  int count = req.count.value(0);
  if (count) {
    end_index = std::min(start_index + count, end_index);
  }

  fxl::RefPtr<VariableResponseContext> response_context =
      fxl::MakeRefCounted<VariableResponseContext>(
          ctx, req, std::move(callback), frame->GetEvalContext(), end_index - start_index);

  // No variables to evaluate.
  if (!response_context->pending_evals) {
    response_context->callback(response_context->response);
    return Err();
  }

  auto it = vars.begin();
  for (auto i = 0; i < end_index; i++, it++) {
    FX_CHECK(it != vars.end());
    // Skip until start index.
    if (i < start_index) {
      continue;
    }

    frame->GetEvalContext()->GetVariableValue(
        RefPtrTo(it->second.get()), [name = it->first, response_context](ErrOrValue value) {
          std::unique_ptr<FormatNode> node;
          if (value.has_error()) {
            node = std::make_unique<FormatNode>(name);
            node->SetDescribedError(value.err());
          } else {
            node = std::make_unique<FormatNode>(name, value.take_value());
            node->set_child_kind(FormatNode::kVariable);
          }
          auto node_ptr = node.get();
          auto on_completion =
              fit::defer_callback([response_context, node = std::move(node)]() mutable {
                response_context->OnComplete(std::move(node));
              });
          FillFormatNodeDescription(node_ptr, FormatOptions(), response_context->eval_context,
                                    std::move(on_completion));
        });
  }
  return Err();
}

Err PopulateChildren(Frame* frame, const dap::VariablesRequest& req, DebugAdapterContext* ctx,
                     std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback) {
  VariablesRecord* info = ctx->VariablesRecordForID(req.variablesReference);
  if (!info) {
    return Err("Invalid variable reference.");
  }
  FormatNode* node_ptr = info->parent ? info->parent.get() : info->child.get();
  if (!node_ptr) {
    return Err("No node pointer for variable.");
  }

  // No children
  if (node_ptr->children().empty()) {
    callback(dap::VariablesResponse());
    return Err();
  }

  int start_index = req.start.value(0);
  int end_index = node_ptr->children().size();
  int count = req.count.value(0);
  if (count) {
    end_index = std::min(start_index + count, end_index);
  }

  fxl::RefPtr<VariableResponseContext> response_context =
      fxl::MakeRefCounted<VariableResponseContext>(
          ctx, req, std::move(callback), frame->GetEvalContext(), end_index - start_index);

  for (auto& child : node_ptr->children()) {
    auto on_completion =
        fit::defer_callback([response_context, child = child->GetWeakPtr()]() mutable {
          response_context->OnComplete(child);
        });
    FillFormatNodeDescription(child.get(), FormatOptions(), response_context->eval_context,
                              std::move(on_completion));
  }
  return Err();
}

Err PopulateLocalVariables(
    Frame* frame, const dap::VariablesRequest& req, DebugAdapterContext* ctx,
    std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback) {
  std::map<std::string, fxl::RefPtr<Variable>> vars;
  const Location& location = frame->GetLocation();
  if (!location.symbol())
    return Err("There is no symbol information for the frame.");

  const Function* function = location.symbol().Get()->As<Function>();
  if (!function)
    return Err("Symbols are corrupt.");

  // Walk upward from the innermost lexical block for the current IP to collect local variables.
  // Using the map allows collecting only the innermost version of a given name, and sorts them as
  // we go.
  //
  // Need owning variable references to copy data out.
  //
  // Note that this does NOT skip "artificial" variables. In the standard these are marked as
  // compiler-generated and we should probably just skip them. The exception is for "this"
  // variables which we do want to show.
  //
  // Be aware that as of this writing there is Clang bug
  // https://bugs.llvm.org/show_bug.cgi?id=49565 which marks the artificial flag on structured
  // bindings incorrectly:
  //
  //   auto [a, b] = GetSomePair();
  //
  // It generates an unnamed std::pair variable without the DW_AT_artificial tag, and "a" and "b"
  // variables WITH the artificial tag. This is backwards from what one would expect and how GCC
  // encodes this (the internal generated variable should be marked artificial, and the ones the
  // user named should not be).
  //
  // Our behavior of showing artificial variables but hiding unnamed ones worked around this bug.
  // It's not clear what other cases in C++ there might be for artificial variables.

  VisitLocalBlocks(function->GetMostSpecificChild(location.symbol_context(), location.address()),
                   [&vars](const CodeBlock* block) {
                     for (const auto& lazy_var : block->variables()) {
                       const Variable* var = lazy_var.Get()->As<Variable>();
                       if (!var)
                         continue;  // Symbols are corrupt.

                       const std::string& name = var->GetAssignedName();
                       if (name.empty())
                         continue;

                       if (vars.find(name) == vars.end())
                         vars[name] = RefPtrTo(var);  // New one.
                     }
                     return VisitResult::kContinue;
                   });

  return PopulateVariableValues(frame, req, ctx, std::move(callback), vars);
}

Err PopulateFunctionArguments(
    Frame* frame, const dap::VariablesRequest& req, DebugAdapterContext* ctx,
    std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback) {
  std::map<std::string, fxl::RefPtr<Variable>> args;
  const Location& location = frame->GetLocation();
  if (!location.symbol())
    return Err("There is no symbol information for the frame.");

  const Function* function = location.symbol().Get()->As<Function>();
  if (!function)
    return Err("Symbols are corrupt.");

  // Add function parameters.
  for (const auto& param : function->parameters()) {
    const Variable* var = param.Get()->As<Variable>();
    if (!var)
      continue;  // Symbols are corrupt.

    const std::string& name = var->GetAssignedName();
    if (!name.empty() && args.find(name) == args.end())
      args[name] = RefPtrTo(var);  // New one.
  }
  return PopulateVariableValues(frame, req, ctx, std::move(callback), args);
}

Err PopulateRegisters(Frame* frame, const dap::VariablesRequest& req, DebugAdapterContext* ctx,
                      std::function<void(dap::ResponseOrError<dap::VariablesResponse>)> callback) {
  // Get general registers.
  // Note: Other registers like vectors, floating points are not reported for now.
  auto* regs = frame->GetRegisterCategorySync(debug::RegisterCategory::kGeneral);
  FX_DCHECK(regs);
  dap::VariablesResponse response;
  for (auto& reg : *regs) {
    dap::Variable var;
    var.name = debug::RegisterIDToString(reg.id);
    uint64_t value = static_cast<uint64_t>(reg.GetValue());
    var.value = to_hex_string(value);
    response.variables.push_back(var);
  }
  callback(response);
  return Err();
}

void OnRequestVariables(
    DebugAdapterContext* ctx, const dap::VariablesRequest& req,
    const std::function<void(dap::ResponseOrError<dap::VariablesResponse>)>& callback) {
  auto* record = ctx->VariablesRecordForID(req.variablesReference);
  if (!record) {
    return callback(dap::Error("Invalid variables reference."));
  }
  auto* frame = ctx->FrameforId(record->frame_id);
  FX_CHECK(frame);

  if (Err err = ctx->CheckStoppedThread(frame->GetThread()); err.has_error()) {
    return callback(dap::Error(err.msg()));
  }

  switch (record->type) {
    case VariablesType::kLocal:
      if (Err err = PopulateLocalVariables(frame, req, ctx, callback); err.has_error()) {
        return callback(dap::Error(err.msg()));
      }
      break;
    case VariablesType::kArguments:
      if (Err err = PopulateFunctionArguments(frame, req, ctx, callback); err.has_error()) {
        return callback(dap::Error(err.msg()));
      }
      break;
    case VariablesType::kRegister:
      if (Err err = PopulateRegisters(frame, req, ctx, callback); err.has_error()) {
        return callback(dap::Error(err.msg()));
      }
      break;
    case VariablesType::kChildVariable:
      if (Err err = PopulateChildren(frame, req, ctx, callback); err.has_error()) {
        return callback(dap::Error(err.msg()));
      }
      break;
    default:
      return callback(dap::Error("Invalid variables type."));
  }
}

}  // namespace zxdb

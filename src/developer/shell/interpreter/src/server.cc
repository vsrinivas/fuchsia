// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/server.h"

#include <memory>
#include <string_view>

#include "lib/async-loop/default.h"
#include "lib/fidl-async/cpp/bind.h"
#include "lib/svc/dir.h"
#include "src/developer/shell/interpreter/src/expressions.h"
#include "src/developer/shell/interpreter/src/instructions.h"
#include "src/developer/shell/interpreter/src/types.h"
#include "src/lib/fxl/logging.h"
#include "zircon/processargs.h"
#include "zircon/status.h"

namespace shell {
namespace interpreter {
namespace server {

inline std::string_view GetView(::fidl::StringView view) {
  return std::string_view(view.data(), view.size());
}

std::unique_ptr<Type> GetType(const llcpp::fuchsia::shell::ShellType& shell_type) {
  if (shell_type.is_undef()) {
    return nullptr;
  }
  if (shell_type.is_builtin_type()) {
    switch (shell_type.builtin_type()) {
      case llcpp::fuchsia::shell::BuiltinType::BOOL:
        return std::make_unique<TypeBool>();
      case llcpp::fuchsia::shell::BuiltinType::CHAR:
        return std::make_unique<TypeChar>();
      case llcpp::fuchsia::shell::BuiltinType::STRING:
        return std::make_unique<TypeString>();
      case llcpp::fuchsia::shell::BuiltinType::INT8:
        return std::make_unique<TypeInt8>();
      case llcpp::fuchsia::shell::BuiltinType::UINT8:
        return std::make_unique<TypeUint8>();
      case llcpp::fuchsia::shell::BuiltinType::INT16:
        return std::make_unique<TypeInt16>();
      case llcpp::fuchsia::shell::BuiltinType::UINT16:
        return std::make_unique<TypeUint16>();
      case llcpp::fuchsia::shell::BuiltinType::INT32:
        return std::make_unique<TypeInt32>();
      case llcpp::fuchsia::shell::BuiltinType::UINT32:
        return std::make_unique<TypeUint32>();
      case llcpp::fuchsia::shell::BuiltinType::INT64:
        return std::make_unique<TypeInt64>();
      case llcpp::fuchsia::shell::BuiltinType::UINT64:
        return std::make_unique<TypeUint64>();
      case llcpp::fuchsia::shell::BuiltinType::INTEGER:
        return std::make_unique<TypeInteger>();
      case llcpp::fuchsia::shell::BuiltinType::FLOAT32:
        return std::make_unique<TypeFloat32>();
      case llcpp::fuchsia::shell::BuiltinType::FLOAT64:
        return std::make_unique<TypeFloat64>();
      default:
        return nullptr;
    }
  }
  return nullptr;
}

std::unique_ptr<Expression> ServerInterpreterContext::GetExpression(const NodeId& node_id) {
  auto result = expressions_.find(node_id);
  if (result == expressions_.end()) {
    return nullptr;
  }
  auto returned_value = std::move(result->second);
  FXL_DCHECK(returned_value != nullptr);
  expressions_.erase(result);
  return returned_value;
}

void ServerInterpreter::EmitError(ExecutionContext* context, std::string error_message) {
  service_->OnError((context == nullptr) ? 0 : context->id(), std::move(error_message));
  if (context != nullptr) {
    context->set_has_errors();
  }
}

void ServerInterpreter::ContextDone(ExecutionContext* context) {
  FXL_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), fuchsia::shell::ExecuteResult::OK);
}

void ServerInterpreter::ContextDoneWithAnalysisError(ExecutionContext* context) {
  FXL_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), fuchsia::shell::ExecuteResult::ANALYSIS_ERROR);
}

void ServerInterpreter::TextResult(ExecutionContext* context, std::string_view text) {
  constexpr size_t kMaxResultSize = 65400;
  size_t offset = 0;
  while (text.size() - offset > kMaxResultSize) {
    service_->OnTextResult(context->id(), std::string(text.data() + offset, kMaxResultSize),
                           /*partial_result=*/true);
    offset += kMaxResultSize;
  }
  service_->OnTextResult(context->id(), std::string(text.data() + offset, text.size() - offset),
                         /*partial_result=*/false);
}

void ServerInterpreter::CreateServerContext(ExecutionContext* context) {
  FXL_DCHECK(contexts_.find(context->id()) == contexts_.end());
  contexts_.emplace(context->id(), std::make_unique<ServerInterpreterContext>(context));
}

void ServerInterpreter::AddExpression(ServerInterpreterContext* context,
                                      std::unique_ptr<Expression> expression, bool global_node) {
  if (global_node) {
    EmitError(context->execution_context(), "Node " + expression->StringId() + " can't be global.");
    return;
  }
  context->AddExpression(std::move(expression));
}

void ServerInterpreter::AddInstruction(ServerInterpreterContext* context,
                                       std::unique_ptr<Instruction> instruction, bool global_node) {
  if (global_node) {
    context->execution_context()->AddPendingInstruction(std::move(instruction));
  } else {
    context->AddInstruction(std::move(instruction));
  }
}

std::unique_ptr<Expression> ServerInterpreter::GetExpression(ServerInterpreterContext* context,
                                                             const NodeId& node_id) {
  if (node_id.node_id == 0) {
    return nullptr;
  }
  auto result = context->GetExpression(node_id);
  if (result == nullptr) {
    EmitError(context->execution_context(), "Can't find node " + node_id.StringId());
    return nullptr;
  }
  return result;
}

void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto server = static_cast<Server*>(untyped_context);
  server->IncommingConnection(service_request);
}

void Service::CreateExecutionContext(uint64_t context_id,
                                     CreateExecutionContextCompleter::Sync completer) {
  auto context = interpreter_->AddContext(context_id);
  if (context != nullptr) {
    interpreter_->CreateServerContext(context);
  }
}

void Service::ExecuteExecutionContext(uint64_t context_id,
                                      ExecuteExecutionContextCompleter::Sync completer) {
  auto context = interpreter_->GetServerContext(context_id);
  if (context == nullptr) {
    interpreter_->EmitError(nullptr,
                            "Execution context " + std::to_string(context_id) + " not defined.");
  } else {
    if (context->PendingNodes()) {
      interpreter_->EmitError(
          context->execution_context(),
          "Pending AST nodes for execution context " + std::to_string(context_id) + ".");
    }
    context->execution_context()->Execute();
    interpreter_->EraseServerContext(context_id);
  }
}

void Service::AddNodes(uint64_t context_id,
                       ::fidl::VectorView<::llcpp::fuchsia::shell::NodeDefinition> nodes,
                       AddNodesCompleter::Sync _completer) {
  auto context = interpreter_->GetServerContext(context_id);
  if (context == nullptr) {
    interpreter_->EmitError(nullptr,
                            "Execution context " + std::to_string(context_id) + " not defined.");
  } else {
    for (const auto& node : nodes) {
      if (node.node.is_integer_literal()) {
        AddIntegerLiteral(context, node.node_id.file_id, node.node_id.node_id,
                          node.node.integer_literal(), node.global_node);
      } else if (node.node.is_variable_definition()) {
        AddVariableDefinition(context, node.node_id.file_id, node.node_id.node_id,
                              node.node.variable_definition(), node.global_node);
      } else {
        interpreter_->EmitError(context->execution_context(),
                                "Can't create node " + std::to_string(node.node_id.file_id) + ":" +
                                    std::to_string(node.node_id.node_id) + " (unknown type).");
      }
    }
  }
}

void Service::AddIntegerLiteral(ServerInterpreterContext* context, uint64_t node_file_id,
                                uint64_t node_node_id,
                                const llcpp::fuchsia::shell::IntegerLiteral& node,
                                bool global_node) {
  if (node.absolute_value.count() > 1) {
    interpreter_->EmitError(context->execution_context(),
                            "Infinite precision integers not supported for node " +
                                std::to_string(node_file_id) + ":" + std::to_string(node_node_id));
    return;
  }
  uint64_t absolute_value = 0;
  if (node.absolute_value.count() > 0) {
    absolute_value = node.absolute_value[0];
  }
  bool negative = node.negative && (absolute_value > 0);
  auto result = std::make_unique<IntegerLiteral>(interpreter(), node_file_id, node_node_id,
                                                 absolute_value, negative);
  interpreter_->AddExpression(context, std::move(result), global_node);
}

void Service::AddVariableDefinition(ServerInterpreterContext* context, uint64_t node_file_id,
                                    uint64_t node_node_id,
                                    const llcpp::fuchsia::shell::VariableDefinition& node,
                                    bool global_node) {
  std::unique_ptr<Expression> initial_value = interpreter_->GetExpression(
      context, NodeId(node.initial_value.file_id, node.initial_value.node_id));
  auto result = std::make_unique<VariableDefinition>(interpreter(), node_file_id, node_node_id,
                                                     GetView(node.name), GetType(node.type),
                                                     node.mutable_value, std::move(initial_value));
  interpreter_->AddInstruction(context, std::move(result), global_node);
}

Server::Server() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

bool Server::Listen() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  if (directory_request == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "error: directory_request was ZX_HANDLE_INVALID";
    return false;
  }

  svc_dir_t* dir = nullptr;
  zx_status_t status = svc_dir_create(loop_.dispatcher(), directory_request, &dir);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "error: svc_dir_create failed: " << status << " ("
                   << zx_status_get_string(status) << ")";
    return false;
  }

  status = svc_dir_add_service(dir, "svc", "fuchsia.shell.Shell", this, connect);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "error: svc_dir_add_service failed: " << status << " ("
                   << zx_status_get_string(status) << ")" << std::endl;
    return false;
  }
  return true;
}

void Server::IncommingConnection(zx_handle_t service_request) {
  fidl::Bind(loop_.dispatcher(), zx::channel(service_request), AddConnection(service_request));
}

}  // namespace server
}  // namespace interpreter
}  // namespace shell

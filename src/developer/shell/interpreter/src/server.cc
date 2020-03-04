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
#include "src/developer/shell/interpreter/src/schema.h"
#include "src/developer/shell/interpreter/src/types.h"
#include "src/lib/fxl/logging.h"
#include "zircon/process.h"
#include "zircon/processargs.h"
#include "zircon/status.h"

namespace shell {
namespace interpreter {
namespace server {

inline std::string_view GetView(::fidl::StringView view) {
  return std::string_view(view.data(), view.size());
}

std::unique_ptr<Type> GetType(ServerInterpreterContext* context, uint64_t node_file_id,
                              uint64_t node_node_id,
                              const llcpp::fuchsia::shell::ShellType& shell_type) {
  if (shell_type.is_undef()) {
    return std::make_unique<TypeUndefined>();
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
        break;
    }
  }
  if (shell_type.is_object_schema()) {
    NodeId node_id(shell_type.object_schema().file_id, shell_type.object_schema().node_id);
    Node* schema_node = context->execution_context()->interpreter()->GetNode(node_id);
    if (schema_node == nullptr) {
      context->execution_context()->EmitError(NodeId(node_file_id, node_node_id),
                                              "Type not found for object");
      return std::make_unique<TypeUndefined>();
    }
    return schema_node->AsObjectSchema()->GetType();
  }
  context->execution_context()->EmitError(NodeId(node_file_id, node_node_id), "Bad type.");
  return std::make_unique<TypeUndefined>();
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

// Return the schema declared by the given node id.
std::unique_ptr<Schema> ServerInterpreterContext::GetSchema(const NodeId& node_id) {
  auto result = schemas_.find(node_id);
  if (result == schemas_.end()) {
    return nullptr;
  }
  auto returned_value = std::move(result->second);
  FXL_DCHECK(returned_value != nullptr);
  return returned_value;
}

// Retrieves the field corresponding to the given node id.
std::unique_ptr<ObjectField> ServerInterpreterContext::GetObjectField(const NodeId& node_id) {
  auto result = fields_.find(node_id);
  if (result == fields_.end()) {
    return nullptr;
  }
  auto returned_value = std::move(result->second);
  FXL_DCHECK(returned_value != nullptr);
  return returned_value;
}

// Retrieves the schema of the field definition for the given node id.
std::unique_ptr<ObjectFieldSchema> ServerInterpreterContext::GetObjectFieldSchema(
    const NodeId& node_id) {
  auto result = object_field_schemas_.find(node_id);
  if (result == object_field_schemas_.end()) {
    return nullptr;
  }
  auto returned_value = std::move(result->second);
  FXL_DCHECK(returned_value != nullptr);
  return returned_value;
}

void ServerInterpreter::EmitError(ExecutionContext* context, std::string error_message) {
  service_->OnError((context == nullptr) ? 0 : context->id(), std::move(error_message));
  if (context != nullptr) {
    context->set_has_errors();
  }
}

void ServerInterpreter::EmitError(ExecutionContext* context, NodeId node_id,
                                  std::string error_message) {
  FXL_DCHECK(context != nullptr);
  std::vector<llcpp::fuchsia::shell::Location> locations;
  llcpp::fuchsia::shell::NodeId fidl_node_id{.file_id = node_id.file_id,
                                             .node_id = node_id.node_id};
  auto builder =
      llcpp::fuchsia::shell::Location::UnownedBuilder().set_node_id(fidl::unowned(&fidl_node_id));
  locations.emplace_back(builder.build());
  service_->OnError(context->id(), locations, std::move(error_message));
  context->set_has_errors();
}

void ServerInterpreter::DumpDone(ExecutionContext* context) {
  FXL_DCHECK(context != nullptr);
  service_->OnDumpDone(context->id());
}

void ServerInterpreter::ContextDone(ExecutionContext* context) {
  FXL_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), llcpp::fuchsia::shell::ExecuteResult::OK);
}

void ServerInterpreter::ContextDoneWithAnalysisError(ExecutionContext* context) {
  FXL_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR);
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
                                      std::unique_ptr<Expression> expression, bool root_node) {
  if (root_node) {
    EmitError(context->execution_context(),
              "Node " + expression->StringId() + " can't be a root node.");
    return;
  }
  context->AddExpression(std::move(expression));
}

void ServerInterpreter::AddSchema(ServerInterpreterContext* context,
                                  std::unique_ptr<Schema> definition, bool root_node) {
  context->AddSchema(std::move(definition));
}

void ServerInterpreter::AddObjectFieldSchema(ServerInterpreterContext* context,
                                             std::unique_ptr<ObjectFieldSchema> definition,
                                             bool root_node) {
  if (root_node) {
    EmitError(context->execution_context(),
              "Node " + definition->StringId() + " can't be a root node.");
    return;
  }
  context->AddObjectFieldSchema(std::move(definition));
}

void ServerInterpreter::AddObjectField(ServerInterpreterContext* context,
                                       std::unique_ptr<ObjectField> definition, bool root_node) {
  if (root_node) {
    EmitError(context->execution_context(),
              "Node " + definition->StringId() + " can't be a root node.");
    return;
  }
  context->AddObjectField(std::move(definition));
}

void ServerInterpreter::AddInstruction(ServerInterpreterContext* context,
                                       std::unique_ptr<Instruction> instruction, bool root_node) {
  if (root_node) {
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

std::unique_ptr<Schema> ServerInterpreter::GetSchema(ServerInterpreterContext* context,
                                                     const NodeId& node_id) {
  if (node_id.node_id == 0) {
    return nullptr;
  }
  auto result = context->GetSchema(node_id);
  if (result == nullptr) {
    EmitError(context->execution_context(), "Can't find schema node " + node_id.StringId());
    return nullptr;
  }
  return result;
}

std::unique_ptr<ObjectFieldSchema> ServerInterpreter::GetObjectFieldSchema(
    ServerInterpreterContext* context, const NodeId& node_id) {
  if (node_id.node_id == 0) {
    return nullptr;
  }
  auto result = context->GetObjectFieldSchema(node_id);
  if (result == nullptr) {
    EmitError(context->execution_context(), "Can't find node " + node_id.StringId());
    return nullptr;
  }
  return result;
}

void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto server = static_cast<Server*>(untyped_context);
  server->IncomingConnection(service_request);
}

void Service::CreateExecutionContext(uint64_t context_id,
                                     CreateExecutionContextCompleter::Sync completer) {
  auto context = interpreter_->AddContext(context_id);
  if (context != nullptr) {
    interpreter_->CreateServerContext(context);
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
                          node.node.integer_literal(), node.root_node);
      } else if (node.node.is_variable_definition()) {
        AddVariableDefinition(context, node.node_id.file_id, node.node_id.node_id,
                              node.node.variable_definition(), node.root_node);
      } else if (node.node.is_object_schema()) {
        AddObjectType(context, node.node_id.file_id, node.node_id.node_id,
                      node.node.object_schema(), node.root_node);
      } else if (node.node.is_field_schema()) {
        AddObjectTypeField(context, node.node_id.file_id, node.node_id.node_id,
                           node.node.field_schema(), node.root_node);
      } else if (node.node.is_object()) {
        AddObject(context, node.node_id.file_id, node.node_id.node_id, node.node.object(),
                  node.root_node);
      } else if (node.node.is_object_field()) {
        AddObjectField(context, node.node_id.file_id, node.node_id.node_id,
                       node.node.object_field(), node.root_node);
      } else if (node.node.is_string_literal()) {
        AddStringLiteral(context, node.node_id.file_id, node.node_id.node_id,
                         node.node.string_literal(), node.root_node);
      } else {
        interpreter_->EmitError(context->execution_context(),
                                "Can't create node " + std::to_string(node.node_id.file_id) + ":" +
                                    std::to_string(node.node_id.node_id) + " (unknown type).");
      }
    }
  }
}

void Service::DumpExecutionContext(uint64_t context_id,
                                   ExecuteExecutionContextCompleter::Sync completer) {
  auto context = interpreter_->GetServerContext(context_id);
  if (context == nullptr) {
    interpreter_->EmitError(nullptr,
                            "Execution context " + std::to_string(context_id) + " not defined.");
  } else {
    context->execution_context()->Dump();
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

void Service::LoadGlobal(::fidl::StringView name, LoadGlobalCompleter::Sync completer) {
  const Variable* variable = interpreter_->SearchGlobal(std::string(name.data(), name.size()));
  Value value;
  std::vector<llcpp::fuchsia::shell::Node> nodes;
  llcpp::fuchsia::shell::IntegerLiteral integer_literal;
  std::vector<uint64_t> absolute_value;
  fidl::StringView string;
  if (variable != nullptr) {
    interpreter_->LoadGlobal(variable, &value);
    switch (value.type()) {
      case ValueType::kUndef:
        break;
      case ValueType::kUint64: {
        absolute_value.emplace_back(value.GetUint64());
        integer_literal.absolute_value = fidl::VectorView(absolute_value);
        llcpp::fuchsia::shell::Node shell_value;
        shell_value.set_integer_literal(
            fidl::unowned_ptr<llcpp::fuchsia::shell::IntegerLiteral>(&integer_literal));
        nodes.emplace_back(std::move(shell_value));
        break;
      }
      case ValueType::kString: {
        String* string_value = value.GetString();
        string.set_data(string_value->value().data());
        string.set_size(string_value->value().size());
        llcpp::fuchsia::shell::Node shell_value;
        shell_value.set_string_literal(fidl::unowned_ptr<fidl::StringView>(&string));
        nodes.emplace_back(std::move(shell_value));
        break;
      }
    }
  }
  completer.Reply(fidl::VectorView(nodes));
}

void Service::AddIntegerLiteral(ServerInterpreterContext* context, uint64_t node_file_id,
                                uint64_t node_node_id,
                                const llcpp::fuchsia::shell::IntegerLiteral& node, bool root_node) {
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
  interpreter_->AddExpression(context, std::move(result), root_node);
}

void Service::AddObjectType(ServerInterpreterContext* context, uint64_t node_file_id,
                            uint64_t node_node_id,
                            const llcpp::fuchsia::shell::ObjectSchemaDefinition& node,
                            bool root_node) {
  std::vector<std::unique_ptr<ObjectFieldSchema>> fields;
  for (auto& field : node.fields) {
    fields.push_back(
        interpreter_->GetObjectFieldSchema(context, NodeId(field.file_id, field.node_id)));
  }
  auto definition =
      std::make_unique<ObjectSchema>(interpreter(), node_file_id, node_node_id, std::move(fields));
  interpreter_->AddSchema(context, std::move(definition), root_node);
}

void Service::AddObjectTypeField(
    ServerInterpreterContext* context, uint64_t node_file_id, uint64_t node_node_id,
    const llcpp::fuchsia::shell::ObjectFieldSchemaDefinition& field_type, bool root_node) {
  auto definition = std::make_unique<ObjectFieldSchema>(
      interpreter(), node_file_id, node_node_id, field_type.name.data(),
      GetType(context, node_file_id, node_node_id, field_type.type));
  interpreter_->AddObjectFieldSchema(context, std::move(definition), root_node);
}

void Service::AddObject(ServerInterpreterContext* context, uint64_t node_file_id,
                        uint64_t node_node_id, const llcpp::fuchsia::shell::ObjectDefinition& node,
                        bool root_node) {
  NodeId schema_node_id(node.object_schema.file_id, node.object_schema.node_id);

  Node* schema_node = interpreter_->GetNode(schema_node_id);
  if (schema_node == nullptr) {
    interpreter_->EmitError(context->execution_context(), "Schema of object variable not defined");
    return;
  }
  if (schema_node->AsObjectSchema() == nullptr) {
    interpreter_->EmitError(context->execution_context(), "IR mismatch: Node type is not schema");
    return;
  }
  std::unique_ptr<Type> type = schema_node->AsObjectSchema()->GetType();
  auto type_object = std::unique_ptr<TypeObject>(type.release()->AsTypeObject());

  std::vector<std::unique_ptr<ObjectField>> fields;
  for (auto& field : node.fields) {
    NodeId field_id(field.file_id, field.node_id);
    std::unique_ptr<ObjectField> field_node = context->GetObjectField(field_id);
    fields.push_back(std::move(field_node));
  }

  auto definition = std::make_unique<Object>(interpreter(), node_file_id, node_node_id,
                                             std::move(type_object), std::move(fields));
  interpreter_->AddExpression(context, std::move(definition), root_node);
}

void Service::AddObjectField(ServerInterpreterContext* context, uint64_t node_file_id,
                             uint64_t node_node_id,
                             const llcpp::fuchsia::shell::ObjectFieldDefinition& node,
                             bool root_node) {
  NodeId schema_id(node.object_field_schema.file_id, node.object_field_schema.node_id);
  Node* type_node = interpreter_->GetNode(schema_id);
  if (type_node == nullptr) {
    interpreter_->EmitError(context->execution_context(), "Type of object field not defined");
    return;
  }
  NodeId value_id(node.value.file_id, node.value.node_id);
  std::unique_ptr<Expression> value = interpreter_->GetExpression(context, value_id);
  auto definition =
      std::make_unique<ObjectField>(interpreter(), node_file_id, node_node_id, std::move(value));
  interpreter_->AddObjectField(context, std::move(definition), root_node);
}

void Service::AddVariableDefinition(ServerInterpreterContext* context, uint64_t node_file_id,
                                    uint64_t node_node_id,
                                    const llcpp::fuchsia::shell::VariableDefinition& node,
                                    bool root_node) {
  std::unique_ptr<Expression> initial_value = interpreter_->GetExpression(
      context, NodeId(node.initial_value.file_id, node.initial_value.node_id));
  auto result = std::make_unique<VariableDefinition>(
      interpreter(), node_file_id, node_node_id, GetView(node.name),
      GetType(context, node_file_id, node_node_id, node.type), node.mutable_value,
      std::move(initial_value));
  interpreter_->AddInstruction(context, std::move(result), root_node);
}

void Service::AddStringLiteral(ServerInterpreterContext* context, uint64_t node_file_id,
                               uint64_t node_node_id, const ::fidl::StringView& node,
                               bool root_node) {
  auto result = std::make_unique<StringLiteral>(interpreter(), node_file_id, node_node_id,
                                                std::string_view(node.data(), node.size()));
  interpreter_->AddExpression(context, std::move(result), root_node);
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

zx_status_t Server::IncomingConnection(zx_handle_t service_request) {
  return fidl::Bind(loop_.dispatcher(), zx::channel(service_request),
                    AddConnection(service_request));
}

}  // namespace server
}  // namespace interpreter
}  // namespace shell

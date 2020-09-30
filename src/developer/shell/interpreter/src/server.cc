// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/server.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <string_view>

#include "lib/async-loop/default.h"
#include "lib/fidl-async/cpp/bind.h"
#include "lib/svc/dir.h"
#include "src/developer/shell/common/ast_builder.h"
#include "src/developer/shell/interpreter/src/expressions.h"
#include "src/developer/shell/interpreter/src/instructions.h"
#include "src/developer/shell/interpreter/src/schema.h"
#include "src/developer/shell/interpreter/src/types.h"
#include "zircon/process.h"
#include "zircon/processargs.h"
#include "zircon/status.h"

namespace shell {
namespace interpreter {
namespace server {

inline std::string_view GetView(const ::fidl::StringView& view) {
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
    std::shared_ptr<ObjectSchema> schema_node =
        context->execution_context()->GetObjectSchema(node_id);
    if (schema_node == nullptr) {
      context->execution_context()->EmitError(NodeId(node_file_id, node_node_id),
                                              "Type not found for object");
      return std::make_unique<TypeUndefined>();
    }
    return ObjectSchema::GetType(schema_node);
  }
  context->execution_context()->EmitError(NodeId(node_file_id, node_node_id), "Bad type.");
  return std::make_unique<TypeUndefined>();
}

class SerializeHelper {
 public:
  SerializeHelper() = default;

  fidl::VectorView<llcpp::fuchsia::shell::Node> nodes() { return builder_.NodesAsVectorView(); }

  struct TypeAndValue {
    shell::console::AstBuilder::NodeId value_id;
    llcpp::fuchsia::shell::ShellType type;
  };

  TypeAndValue Set(const Value& value) {
    TypeAndValue id;
    switch (value.type()) {
      case ValueType::kUndef: {
        id.value_id.file_id = -1;
        id.value_id.node_id = -1;
        fidl::aligned<bool> undef = true;
        auto undef_ptr = builder_.ManageCopyOf(&undef);
        id.type = llcpp::fuchsia::shell::ShellType::WithUndef(fidl::unowned_ptr(undef_ptr));
        break;
      }
      case ValueType::kInt8:
        id.value_id = builder_.AddIntegerLiteral(value.GetInt8());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::INT8);
        break;
      case ValueType::kUint8:
        id.value_id = builder_.AddIntegerLiteral(value.GetUint8());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT8);
        break;
      case ValueType::kInt16:
        id.value_id = builder_.AddIntegerLiteral(value.GetInt16());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::INT16);
        break;
      case ValueType::kUint16:
        id.value_id = builder_.AddIntegerLiteral(value.GetUint16());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT16);
        break;
      case ValueType::kInt32:
        id.value_id = builder_.AddIntegerLiteral(value.GetInt32());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::INT32);
        break;
      case ValueType::kUint32:
        id.value_id = builder_.AddIntegerLiteral(value.GetUint32());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT32);
        break;
      case ValueType::kInt64:
        id.value_id = builder_.AddIntegerLiteral(value.GetInt64());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::INT64);
        break;
      case ValueType::kUint64:
        id.value_id = builder_.AddIntegerLiteral(value.GetUint64());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT64);
        break;
      // Float ?
      case ValueType::kString:
        id.value_id = builder_.AddStringLiteral(value.GetString()->value());
        id.type = GetBuiltin(llcpp::fuchsia::shell::BuiltinType::STRING);
        break;
      case ValueType::kObject: {
        builder_.OpenObject();
        Object* object = value.GetObject();
        const std::shared_ptr<ObjectSchema> schema = object->schema();
        for (auto& field : schema->fields()) {
          auto value = object->GetField(field.get());
          auto expression_id = Set(*value);
          builder_.AddField(field->name(), expression_id.value_id, std::move(expression_id.type));
        }
        shell::console::AstBuilder::NodePair value_type = builder_.CloseObject();
        id.value_id = value_type.value_node;
        auto type_ptr = builder_.ManageCopyOf(&value_type.schema_node);
        id.type = llcpp::fuchsia::shell::ShellType::WithObjectSchema(fidl::unowned_ptr(type_ptr));
        break;
      }
    }
    return id;
  }

  llcpp::fuchsia::shell::ShellType GetBuiltin(llcpp::fuchsia::shell::BuiltinType type) {
    llcpp::fuchsia::shell::BuiltinType* type_ptr = builder_.ManageCopyOf(&type);
    return llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned_ptr(type_ptr));
  }

 private:
  shell::console::AstBuilder builder_;
};

// - ServerInterpreterContext ----------------------------------------------------------------------

std::unique_ptr<Expression> ServerInterpreterContext::GetExpression(const NodeId& node_id) {
  auto result = expressions_.find(node_id);
  if (result == expressions_.end()) {
    return nullptr;
  }
  auto returned_value = std::move(result->second);
  FX_DCHECK(returned_value != nullptr);
  expressions_.erase(result);
  return returned_value;
}

// Retrieves the field corresponding to the given node id.
std::unique_ptr<ObjectDeclarationField> ServerInterpreterContext::GetObjectField(
    const NodeId& node_id) {
  auto result = fields_.find(node_id);
  if (result == fields_.end()) {
    return nullptr;
  }
  auto returned_value = std::move(result->second);
  FX_DCHECK(returned_value != nullptr);
  return returned_value;
}

// Retrieves the schema of the field definition for the given node id.
std::shared_ptr<ObjectFieldSchema> ServerInterpreterContext::GetObjectFieldSchema(
    const NodeId& node_id) {
  auto result = object_field_schemas_.find(node_id);
  if (result == object_field_schemas_.end()) {
    return nullptr;
  }
  FX_DCHECK(result->second != nullptr);
  return result->second;
}

// - ServerInterpreter -----------------------------------------------------------------------------

void ServerInterpreter::EmitError(ExecutionContext* context, std::string error_message) {
  service_->OnError((context == nullptr) ? 0 : context->id(), std::move(error_message));
  if (context != nullptr) {
    context->set_has_errors();
  }
}

void ServerInterpreter::EmitError(ExecutionContext* context, NodeId node_id,
                                  std::string error_message) {
  FX_DCHECK(context != nullptr);
  std::vector<llcpp::fuchsia::shell::Location> locations;
  llcpp::fuchsia::shell::NodeId fidl_node_id{.file_id = node_id.file_id,
                                             .node_id = node_id.node_id};
  auto builder = llcpp::fuchsia::shell::Location::UnownedBuilder().set_node_id(
      fidl::unowned_ptr(&fidl_node_id));
  locations.emplace_back(builder.build());
  service_->OnError(context->id(), locations, std::move(error_message));
  context->set_has_errors();
}

void ServerInterpreter::DumpDone(ExecutionContext* context) {
  FX_DCHECK(context != nullptr);
  service_->OnDumpDone(context->id());
}

void ServerInterpreter::ContextDone(ExecutionContext* context) {
  FX_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), llcpp::fuchsia::shell::ExecuteResult::OK);
}

void ServerInterpreter::ContextDoneWithAnalysisError(ExecutionContext* context) {
  FX_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR);
}

void ServerInterpreter::ContextDoneWithExecutionError(ExecutionContext* context) {
  FX_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), llcpp::fuchsia::shell::ExecuteResult::EXECUTION_ERROR);
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

void ServerInterpreter::Result(ExecutionContext* context, const Value& result) {
  SerializeHelper helper;
  helper.Set(result);
  service_->OnResult(context->id(), helper.nodes(), false);
}

void ServerInterpreter::CreateServerContext(ExecutionContext* context) {
  FX_DCHECK(contexts_.find(context->id()) == contexts_.end());
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

void ServerInterpreter::AddInstruction(ServerInterpreterContext* context,
                                       std::unique_ptr<Instruction> instruction, bool root_node) {
  if (root_node) {
    context->execution_context()->AddPendingInstruction(std::move(instruction));
  } else {
    context->AddInstruction(std::move(instruction));
  }
}

void ServerInterpreter::AddObjectSchema(ServerInterpreterContext* context,
                                        std::shared_ptr<ObjectSchema> definition, bool root_node) {
  if (root_node) {
    EmitError(context->execution_context(),
              "Node " + definition->StringId() + ": classes not implemented.");
  } else {
    context->execution_context()->AddObjectSchema(definition);
  }
}

void ServerInterpreter::AddObjectFieldSchema(ServerInterpreterContext* context,
                                             std::shared_ptr<ObjectFieldSchema> definition,
                                             bool root_node) {
  if (root_node) {
    EmitError(context->execution_context(),
              "Node " + definition->StringId() + " can't be a root node.");
    return;
  }
  context->AddObjectFieldSchema(definition);
}

void ServerInterpreter::AddObjectField(ServerInterpreterContext* context,
                                       std::unique_ptr<ObjectDeclarationField> definition,
                                       bool root_node) {
  if (root_node) {
    EmitError(context->execution_context(),
              "Node " + definition->StringId() + " can't be a root node.");
    return;
  }
  context->AddObjectField(std::move(definition));
}

std::unique_ptr<Expression> ServerInterpreter::GetNullableExpression(
    ServerInterpreterContext* context, const NodeId& node_id) {
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

std::unique_ptr<Expression> ServerInterpreter::GetExpression(ServerInterpreterContext* context,
                                                             const NodeId& container_id,
                                                             const std::string& member,
                                                             const NodeId& node_id) {
  if (node_id.node_id == 0) {
    EmitError(context->execution_context(), container_id, member + " can't be null.");
    return nullptr;
  }
  auto result = context->GetExpression(node_id);
  if (result == nullptr) {
    EmitError(context->execution_context(), container_id,
              "Can't find node " + node_id.StringId() + " for " + member + ".");
    return nullptr;
  }
  return result;
}

std::shared_ptr<ObjectFieldSchema> ServerInterpreter::GetObjectFieldSchema(
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

// - connect ---------------------------------------------------------------------------------------

void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto server = static_cast<Server*>(untyped_context);
  server->IncomingConnection(service_request);
}

// - Service ---------------------------------------------------------------------------------------

void Service::CreateExecutionContext(uint64_t context_id,
                                     CreateExecutionContextCompleter::Sync& completer) {
  auto context = interpreter_->AddContext(context_id);
  if (context != nullptr) {
    interpreter_->CreateServerContext(context);
  }
}

void Service::AddNodes(uint64_t context_id,
                       ::fidl::VectorView<::llcpp::fuchsia::shell::NodeDefinition> nodes,
                       AddNodesCompleter::Sync& _completer) {
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
        AddObjectSchema(context, node.node_id.file_id, node.node_id.node_id,
                        node.node.object_schema(), node.root_node);
      } else if (node.node.is_field_schema()) {
        AddObjectSchemaField(context, node.node_id.file_id, node.node_id.node_id,
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
      } else if (node.node.is_variable()) {
        AddVariable(context, node.node_id.file_id, node.node_id.node_id, node.node.variable(),
                    node.root_node);
      } else if (node.node.is_emit_result()) {
        AddEmitResult(context, node.node_id.file_id, node.node_id.node_id, node.node.emit_result(),
                      node.root_node);
      } else if (node.node.is_assignment()) {
        AddAssignment(context, node.node_id.file_id, node.node_id.node_id, node.node.assignment(),
                      node.root_node);
      } else if (node.node.is_addition()) {
        AddAddition(context, node.node_id.file_id, node.node_id.node_id, node.node.addition(),
                    node.root_node);
      } else {
        interpreter_->EmitError(context->execution_context(),
                                "Can't create node " + std::to_string(node.node_id.file_id) + ":" +
                                    std::to_string(node.node_id.node_id) + " (unknown type).");
      }
    }
  }
}

void Service::DumpExecutionContext(uint64_t context_id,
                                   ExecuteExecutionContextCompleter::Sync& completer) {
  auto context = interpreter_->GetServerContext(context_id);
  if (context == nullptr) {
    interpreter_->EmitError(nullptr,
                            "Execution context " + std::to_string(context_id) + " not defined.");
  } else {
    context->execution_context()->Dump();
  }
}

void Service::ExecuteExecutionContext(uint64_t context_id,
                                      ExecuteExecutionContextCompleter::Sync& completer) {
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

void Service::Shutdown(ShutdownCompleter::Sync& completer) {
  // Shutdown the interpreter. If we have some memory leaks, this will generate errors.
  std::vector<std::string> errors;
  interpreter_->Shutdown(&errors);
  // Send the potential errors to the caller.
  std::vector<fidl::StringView> error_view;
  for (const auto& error : errors) {
    error_view.emplace_back(fidl::unowned_ptr(error.c_str()), error.size());
  }
  completer.Reply(fidl::unowned_vec(error_view));
  // Erase the service. That also closes the handle which means that if the client sends a request
  // after the shutdown, it will receive a ZX_ERR_PEER_CLOSED.
  server_->EraseService(this);
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

void Service::AddObjectSchema(ServerInterpreterContext* context, uint64_t node_file_id,
                              uint64_t node_node_id,
                              const llcpp::fuchsia::shell::ObjectSchemaDefinition& node,
                              bool root_node) {
  std::vector<std::shared_ptr<ObjectFieldSchema>> fields;
  for (auto& field : node.fields) {
    fields.push_back(
        interpreter_->GetObjectFieldSchema(context, NodeId(field.file_id, field.node_id)));
  }
  auto definition =
      std::make_shared<ObjectSchema>(interpreter(), node_file_id, node_node_id, std::move(fields));
  interpreter_->AddObjectSchema(context, definition, root_node);
}

void Service::AddObjectSchemaField(
    ServerInterpreterContext* context, uint64_t node_file_id, uint64_t node_node_id,
    const llcpp::fuchsia::shell::ObjectFieldSchemaDefinition& field_type, bool root_node) {
  auto definition = std::make_shared<ObjectFieldSchema>(
      interpreter(), node_file_id, node_node_id, field_type.name.data(),
      GetType(context, node_file_id, node_node_id, field_type.type));
  interpreter_->AddObjectFieldSchema(context, definition, root_node);
}

void Service::AddObject(ServerInterpreterContext* context, uint64_t node_file_id,
                        uint64_t node_node_id, const llcpp::fuchsia::shell::ObjectDefinition& node,
                        bool root_node) {
  NodeId schema_node_id(node.object_schema.file_id, node.object_schema.node_id);

  std::shared_ptr<ObjectSchema> object_schema =
      context->execution_context()->GetObjectSchema(schema_node_id);
  if (object_schema == nullptr) {
    interpreter_->EmitError(context->execution_context(), "Schema of object variable not defined");
    return;
  }

  std::vector<std::unique_ptr<ObjectDeclarationField>> fields;
  fields.reserve(node.fields.count());

  for (auto& field : node.fields) {
    NodeId field_id(field.file_id, field.node_id);
    std::unique_ptr<ObjectDeclarationField> field_node = context->GetObjectField(field_id);
    fields.emplace_back(std::move(field_node));
  }

  auto definition = std::make_unique<ObjectDeclaration>(interpreter(), node_file_id, node_node_id,
                                                        object_schema, std::move(fields));
  interpreter_->AddExpression(context, std::move(definition), root_node);
}

void Service::AddObjectField(ServerInterpreterContext* context, uint64_t node_file_id,
                             uint64_t node_node_id,
                             const llcpp::fuchsia::shell::ObjectFieldDefinition& node,
                             bool root_node) {
  NodeId schema_id(node.object_field_schema.file_id, node.object_field_schema.node_id);
  std::shared_ptr<ObjectFieldSchema> field_schema =
      interpreter_->GetObjectFieldSchema(context, schema_id);
  NodeId value_id(node.value.file_id, node.value.node_id);
  std::unique_ptr<Expression> value = interpreter_->GetExpression(
      context, NodeId(node_file_id, node_node_id), "expression", value_id);
  auto definition = std::make_unique<ObjectDeclarationField>(
      interpreter(), node_file_id, node_node_id, field_schema, std::move(value));
  interpreter_->AddObjectField(context, std::move(definition), root_node);
}

void Service::AddVariableDefinition(ServerInterpreterContext* context, uint64_t node_file_id,
                                    uint64_t node_node_id,
                                    const llcpp::fuchsia::shell::VariableDefinition& node,
                                    bool root_node) {
  std::unique_ptr<Expression> initial_value = interpreter_->GetNullableExpression(
      context, NodeId(node.initial_value.file_id, node.initial_value.node_id));
  std::unique_ptr<Type> type = GetType(context, node_file_id, node_node_id, node.type);
  if (type->IsUndefined()) {
    interpreter_->EmitError(context->execution_context(), NodeId(node_file_id, node_node_id),
                            "Type not defined.");
    return;
  }
  auto result = std::make_unique<VariableDefinition>(interpreter(), node_file_id, node_node_id,
                                                     GetView(node.name), std::move(type),
                                                     node.mutable_value, std::move(initial_value));
  interpreter_->AddInstruction(context, std::move(result), root_node);
}

void Service::AddStringLiteral(ServerInterpreterContext* context, uint64_t node_file_id,
                               uint64_t node_node_id, const ::fidl::StringView& node,
                               bool root_node) {
  auto result = std::make_unique<StringLiteral>(interpreter(), node_file_id, node_node_id,
                                                std::string_view(node.data(), node.size()));
  interpreter_->AddExpression(context, std::move(result), root_node);
}

void Service::AddVariable(ServerInterpreterContext* context, uint64_t node_file_id,
                          uint64_t node_node_id, const fidl::StringView& name, bool root_node) {
  auto result = std::make_unique<ExpressionVariable>(interpreter(), node_file_id, node_node_id,
                                                     std::string(name.data(), name.size()));
  interpreter_->AddExpression(context, std::move(result), root_node);
}

void Service::AddEmitResult(ServerInterpreterContext* context, uint64_t node_file_id,
                            uint64_t node_node_id, const llcpp::fuchsia::shell::NodeId& node,
                            bool root_node) {
  std::unique_ptr<Expression> expression =
      interpreter_->GetExpression(context, NodeId(node_file_id, node_node_id), "expression",
                                  NodeId(node.file_id, node.node_id));
  auto result = std::make_unique<EmitResult>(interpreter(), node_file_id, node_node_id,
                                             std::move(expression));
  interpreter_->AddInstruction(context, std::move(result), root_node);
}

void Service::AddAssignment(ServerInterpreterContext* context, uint64_t node_file_id,
                            uint64_t node_node_id, const llcpp::fuchsia::shell::Assignment& node,
                            bool root_node) {
  std::unique_ptr<Expression> destination =
      interpreter_->GetExpression(context, NodeId(node_file_id, node_node_id), "destination",
                                  NodeId(node.destination.file_id, node.destination.node_id));
  std::unique_ptr<Expression> source =
      interpreter_->GetExpression(context, NodeId(node_file_id, node_node_id), "source",
                                  NodeId(node.source.file_id, node.source.node_id));
  auto result = std::make_unique<Assignment>(interpreter(), node_file_id, node_node_id,
                                             std::move(destination), std::move(source));
  interpreter_->AddInstruction(context, std::move(result), root_node);
}

void Service::AddAddition(ServerInterpreterContext* context, uint64_t node_file_id,
                          uint64_t node_node_id, const llcpp::fuchsia::shell::Addition& node,
                          bool root_node) {
  std::unique_ptr<Expression> left =
      interpreter_->GetExpression(context, NodeId(node_file_id, node_node_id), "left",
                                  NodeId(node.left.file_id, node.left.node_id));
  std::unique_ptr<Expression> right =
      interpreter_->GetExpression(context, NodeId(node_file_id, node_node_id), "right",
                                  NodeId(node.right.file_id, node.right.node_id));
  auto result = std::make_unique<Addition>(interpreter(), node_file_id, node_node_id,
                                           node.with_exceptions, std::move(left), std::move(right));
  interpreter_->AddExpression(context, std::move(result), root_node);
}

// - Server ----------------------------------------------------------------------------------------

Server::Server(async::Loop* loop) : loop_(loop) {}

bool Server::Listen() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  if (directory_request == ZX_HANDLE_INVALID) {
    FX_LOGS(ERROR) << "error: directory_request was ZX_HANDLE_INVALID";
    return false;
  }

  svc_dir_t* dir = nullptr;
  zx_status_t status = svc_dir_create(loop()->dispatcher(), directory_request, &dir);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "error: svc_dir_create failed: " << status << " ("
                   << zx_status_get_string(status) << ")";
    return false;
  }

  status = svc_dir_add_service(dir, "svc", "fuchsia.shell.Shell", this, connect);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "error: svc_dir_add_service failed: " << status << " ("
                   << zx_status_get_string(status) << ")" << std::endl;
    return false;
  }
  return true;
}

zx_status_t Server::IncomingConnection(zx_handle_t service_request) {
  return fidl::BindSingleInFlightOnly(loop()->dispatcher(), zx::channel(service_request),
                                      AddConnection(service_request));
}

}  // namespace server
}  // namespace interpreter
}  // namespace shell

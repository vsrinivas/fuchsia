// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/command.h"

#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>

#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

#include "src/developer/shell/parser/ast.h"
#include "src/developer/shell/parser/parser.h"

namespace shell::console {

Command::Command() = default;

Command::~Command() = default;

namespace {

// Walk a parse tree for errors and collect their messages into the given output stringstream.
void CollectErrors(const parser::ast::Node& node, std::stringstream* out) {
  if (auto err = node.AsError()) {
    if (out->tellp() > 0) {
      (*out) << "\n";
    }

    (*out) << err->message();
  } else {
    for (const auto& child : node.Children()) {
      CollectErrors(*child, out);
    }
  }
}

// Walk a parse tree for errors and collect their messages.
std::string CollectErrors(const parser::ast::Node& node) {
  std::stringstream out;
  CollectErrors(node, &out);
  return out.str();
}

struct IdAndType {
  AstBuilder::NodeId id;
  llcpp::fuchsia::shell::ShellType type;
};

// Visitor for loading a parser AST into a FIDL AST.
class NodeASTVisitor : public parser::ast::NodeVisitor<IdAndType> {
 public:
  explicit NodeASTVisitor(AstBuilder* builder) : builder_(builder) {}

  IdAndType VisitNode(const parser::ast::Node& node) override {
    FX_NOTREACHED() << "Parser produced unknown node type.";
    return {};
  }

  IdAndType VisitProgram(const parser::ast::Program& node) override {
    // TODO: Multiple statements.
    for (const auto& child : node.Children()) {
      if (auto ch = child->AsVariableDecl()) {
        auto ret = VisitVariableDecl(*ch);
        // Return the value of the variable to the command line when done evaluating.
        builder_->AddEmitResult(builder_->AddVariable(ch->identifier()));
        return ret;
      }
    }

    FX_NOTREACHED();
    return {};
  }

  IdAndType VisitVariableDecl(const parser::ast::VariableDecl& node) override {
    IdAndType expression = node.expression()->Visit(this);
    AstBuilder::NodeId id = builder_->AddVariableDeclaration(
        node.identifier(), std::move(expression.type), expression.id, false);

    return {.id = id, .type = llcpp::fuchsia::shell::ShellType()};
  }

  IdAndType VisitInteger(const parser::ast::Integer& node) override {
    AstBuilder::NodeId id = builder_->AddIntegerLiteral(node.value());
    llcpp::fuchsia::shell::BuiltinType builtin_type = llcpp::fuchsia::shell::BuiltinType::INTEGER;
    llcpp::fuchsia::shell::BuiltinType* type_ptr = builder_->ManageCopyOf(&builtin_type);
    llcpp::fuchsia::shell::ShellType type =
        llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned_ptr(type_ptr));

    return {.id = id, .type = std::move(type)};
  }

  IdAndType VisitIdentifier(const parser::ast::Identifier& node) override {
    FX_NOTREACHED() << "Variable fetches are unimplemented." << node.identifier();
    return {};
  }

  IdAndType VisitPath(const parser::ast::Path& node) override {
    FX_NOTREACHED() << "Paths are unimplemented.";
    return {};
  }

  IdAndType VisitAddSub(const parser::ast::AddSub& node) override {
    FX_DCHECK(node.type() == parser::ast::AddSub::kAdd) << "Subtraction is unimplemented.";
    AstBuilder::NodeId a_id = node.a()->Visit(this).id;
    IdAndType b_value = node.b()->Visit(this);
    AstBuilder::NodeId b_id = b_value.id;

    AstBuilder::NodeId id = builder_->AddAddition(/*with_exceptions=*/false, a_id, b_id);
    return {.id = id, .type = std::move(b_value.type)};
  }

  IdAndType VisitExpression(const parser::ast::Expression& node) override {
    FX_DCHECK(node.Children().size() > 0);
    return node.Children()[0]->Visit(this);
  }

  IdAndType VisitString(const parser::ast::String& node) override {
    AstBuilder::NodeId id = builder_->AddStringLiteral(node.value());
    llcpp::fuchsia::shell::BuiltinType builtin_type = llcpp::fuchsia::shell::BuiltinType::STRING;
    llcpp::fuchsia::shell::BuiltinType* type_ptr = builder_->ManageCopyOf(&builtin_type);
    llcpp::fuchsia::shell::ShellType type =
        llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned_ptr(type_ptr));
    return {.id = id, .type = std::move(type)};
  }

  IdAndType VisitObject(const parser::ast::Object& node) override {
    builder_->OpenObject();

    for (const auto& field : node.fields()) {
      field->Visit(this);
    }

    auto result = builder_->CloseObject();
    AstBuilder::NodeId id = result.value_node;

    llcpp::fuchsia::shell::NodeId shell_id;
    shell_id = result.schema_node;
    llcpp::fuchsia::shell::NodeId* id_ptr = builder_->ManageCopyOf(&shell_id);
    llcpp::fuchsia::shell::ShellType type =
        llcpp::fuchsia::shell::ShellType::WithObjectSchema(fidl::unowned_ptr(id_ptr));

    return {.id = id, .type = std::move(type)};
  }

  IdAndType VisitField(const parser::ast::Field& node) override {
    IdAndType value = node.value()->Visit(this);
    builder_->AddField(node.name(), value.id, std::move(value.type));
    return {};
  }

 private:
  AstBuilder* builder_;
};

}  // namespace

bool Command::Parse(const std::string& line) {
  if (line.empty()) {
    return true;
  }

  auto node = parser::Parse(line);

  if (!node) {
    parse_error_ = Err(ErrorType::kBadParse, "Command not recognized.");
    return false;
  }

  if (node->HasErrors()) {
    parse_error_ = Err(ErrorType::kBadParse, CollectErrors(*node));
    return false;
  }

  auto program = node->AsProgram();
  FX_DCHECK(program) << "Parse did not yield a program node!";

  // TODO: Change the file ID to something useful.
  AstBuilder builder(1);
  NodeASTVisitor visitor(&builder);
  IdAndType value = program->Visit(&visitor);
  builder.SetRoot(value.id);
  accumulated_nodes_ = std::move(builder);

  return true;
}

}  // namespace shell::console

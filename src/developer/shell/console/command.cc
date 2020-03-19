// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/command.h"

#include <stdlib.h>

#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

#include "src/developer/shell/parser/ast.h"
#include "src/developer/shell/parser/parser.h"
#include "src/lib/syslog/cpp/logger.h"

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

// Visitor for loading a parser AST into a FIDL AST.
class NodeASTVisitor : public parser::ast::NodeVisitor {
 public:
  explicit NodeASTVisitor(AstBuilder* builder) : builder_(builder) {}
  AstBuilder::NodeId id() const { return id_; }

  void VisitNode(const parser::ast::Node& node) override {
    FX_NOTREACHED() << "Parser produced unknown node type.";
  }

  void VisitProgram(const parser::ast::Program& node) override {
    // TODO: Multiple statements.
    for (const auto& child : node.Children()) {
      if (auto ch = child->AsVariableDecl()) {
        return VisitVariableDecl(*ch);
      }
    }

    FX_NOTREACHED();
  }

  void VisitVariableDecl(const parser::ast::VariableDecl& node) override {
    node.expression()->Visit(this);
    id_ = builder_->AddVariableDeclaration(node.identifier(), std::move(type_), id_, false);
  }

  void VisitInteger(const parser::ast::Integer& node) override {
    id_ = builder_->AddIntegerLiteral(node.value());
    llcpp::fuchsia::shell::BuiltinType type = llcpp::fuchsia::shell::BuiltinType::INTEGER;
    llcpp::fuchsia::shell::BuiltinType* type_ptr = builder_->ManageCopyOf(&type);
    type_ = llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned_ptr(type_ptr));
  }

  void VisitIdentifier(const parser::ast::Identifier& node) override {
    FX_NOTREACHED() << "Variable fetches are unimplemented." << node.identifier();
  }

  void VisitPath(const parser::ast::Path& node) override {
    FX_NOTREACHED() << "Paths are unimplemented.";
  }

  void VisitExpression(const parser::ast::Expression& node) override {
    FX_DCHECK(node.Children().size() > 0);
    node.Children()[0]->Visit(this);
  }

  void VisitString(const parser::ast::String& node) override {
    id_ = builder_->AddStringLiteral(node.value());
    llcpp::fuchsia::shell::BuiltinType type = llcpp::fuchsia::shell::BuiltinType::STRING;
    llcpp::fuchsia::shell::BuiltinType* type_ptr = builder_->ManageCopyOf(&type);
    type_ = llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned_ptr(type_ptr));
  }

  void VisitObject(const parser::ast::Object& node) override {
    builder_->OpenObject();

    for (const auto& field : node.fields()) {
      field->Visit(this);
    }

    auto result = builder_->CloseObject();
    id_ = result.value_node;

    llcpp::fuchsia::shell::NodeId id;
    id = result.schema_node;
    llcpp::fuchsia::shell::NodeId* id_ptr = builder_->ManageCopyOf(&id);
    type_ = llcpp::fuchsia::shell::ShellType::WithObjectSchema(fidl::unowned_ptr(id_ptr));
  }

  void VisitField(const parser::ast::Field& node) override {
    node.value()->Visit(this);
    builder_->AddField(node.name(), id_, std::move(type_));
  }

 private:
  AstBuilder* builder_;
  AstBuilder::NodeId id_;
  llcpp::fuchsia::shell::ShellType type_;
};

}  // namespace

bool Command::Parse(const std::string& line) {
  if (line.empty()) {
    return true;
  }

  auto node = parser::Parse(line);

  if (node->HasErrors()) {
    parse_error_ = Err(ErrorType::kBadParse, CollectErrors(*node));
    return false;
  }

  auto program = node->AsProgram();
  FX_DCHECK(program) << "Parse did not yield a program node!";

  // TODO: Change the file ID to something useful.
  AstBuilder builder(1);
  NodeASTVisitor visitor(&builder);
  program->Visit(&visitor);
  builder.SetRoot(visitor.id());
  accumulated_nodes_ = std::move(builder);

  return true;
}

}  // namespace shell::console

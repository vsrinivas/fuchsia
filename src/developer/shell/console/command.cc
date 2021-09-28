// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/command.h"

#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>

#include <iomanip>
#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

#include "src/developer/shell/parser/ast.h"
#include "src/developer/shell/parser/parser.h"

namespace shell::console {

// TODO: Change the file ID to something useful.
Command::Command() : accumulated_nodes_(1) {}

Command::~Command() = default;

namespace {

// Walk a parse tree for errors and collect their messages into the given output stringstream.
void CollectErrors(std::string_view line, const parser::ast::Node& node, std::stringstream* out) {
  if (auto err = node.AsError()) {
    size_t err_end = err->start() + err->Size();
    size_t line_start_offset = 0;
    size_t line_start = 1;

    for (size_t i = 0; i < err->start(); i++) {
      if (line[i] == '\n') {
        line_start_offset = i + 1;
        line_start++;
      }
    }

    size_t line_end = line_start;

    for (size_t i = line_start_offset; i < err_end; i++) {
      if (line[i] == '\n') {
        line_end++;
      }
    }

    size_t line_pad = std::to_string(line_end).size();
    size_t line_number = line_start;
    size_t start = line_start_offset;

    do {
      auto prev_width = out->width(line_pad);
      (*out) << line_number;
      out->width(prev_width);
      (*out) << ": ";
      size_t end = line.find('\n', start);

      if (end == std::string::npos) {
        end = line.size();
      }

      (*out) << line.substr(start, end - start) << "\n" << std::string(line_pad + 2, ' ');

      for (size_t i = start; i <= end; i++) {
        if (i == err->start()) {
          (*out) << "^";

          if (i != err_end) {
            continue;
          }
        }

        if (i < err->start()) {
          (*out) << " ";
        } else if (i == err_end) {
          (*out) << " " << err->message();
          break;
        } else {
          (*out) << "~";
        }
      }

      (*out) << "\n\n";
      start = end + 1;
      line_number += 1;
    } while (start <= err_end);
  } else {
    for (const auto& child : node.Children()) {
      CollectErrors(line, *child, out);
    }
  }
}

// Walk a parse tree for errors and collect their messages.
std::string CollectErrors(std::string_view line, const parser::ast::Node& node) {
  std::stringstream out;
  CollectErrors(line, node, &out);
  return out.str();
}

struct IdAndType {
  AstBuilder::NodeId id;
  fuchsia_shell::wire::ShellType type;
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

    return {.id = id, .type = fuchsia_shell::wire::ShellType()};
  }

  IdAndType VisitInteger(const parser::ast::Integer& node) override {
    AstBuilder::NodeId id = builder_->AddIntegerLiteral(node.value());
    return {.id = id,
            .type = fuchsia_shell::wire::ShellType::WithBuiltinType(
                fuchsia_shell::wire::BuiltinType::kInteger)};
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
    return {.id = id,
            .type = fuchsia_shell::wire::ShellType::WithBuiltinType(
                fuchsia_shell::wire::BuiltinType::kString)};
  }

  IdAndType VisitObject(const parser::ast::Object& node) override {
    builder_->OpenObject();

    for (const auto& field : node.fields()) {
      field->Visit(this);
    }

    auto result = builder_->CloseObject();
    AstBuilder::NodeId id = result.value_node;
    return {.id = id,
            .type = fuchsia_shell::wire::ShellType::WithObjectSchema(builder_->allocator(),
                                                                     result.schema_node)};
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

  FX_DCHECK(node) << "Error handling failed.";

  if (node->HasErrors()) {
    parse_error_ = CollectErrors(line, *node);
    return false;
  }

  auto program = node->AsProgram();
  FX_DCHECK(program) << "Parse did not yield a program node!";

  NodeASTVisitor visitor(&accumulated_nodes_);
  IdAndType value = program->Visit(&visitor);
  accumulated_nodes_.SetRoot(value.id);

  return true;
}

}  // namespace shell::console

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CPP_VISITOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CPP_VISITOR_H_

#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

class CppVariable {
 public:
  CppVariable(std::string_view name, const Value* value, const Type* for_type)
      : name_(name), value_(value), for_type_(for_type) {}

  virtual ~CppVariable() = default;

  std::string name() const { return name_; }

  const Value* value() const { return value_; }
  const Type* for_type() const { return for_type_; }

  virtual void GenerateDeclaration(PrettyPrinter& printer) const {
    this->GenerateTypeAndName(printer);
    printer << ";\n";
  }

  virtual void GenerateInitialization(PrettyPrinter& printer, const char* suffix = "") const {
    this->GenerateTypeAndName(printer, suffix);
    printer << " = ";
    this->GenerateLiteralValue(printer);
    printer << ";\n";
  }

  virtual void GenerateName(PrettyPrinter& printer, const char* suffix = "") const {
    printer << name() << suffix;
  }

  virtual void GenerateType(PrettyPrinter& printer) const { printer << for_type()->CppName(); }

  virtual void GenerateTypeAndName(PrettyPrinter& printer, const char* suffix = "") const {
    this->GenerateType(printer);
    printer << " ";
    this->GenerateName(printer, suffix);
  }

  virtual void GenerateLiteralValue(PrettyPrinter& printer) const {
    value_->PrettyPrint(for_type_, printer);
  }

  virtual inline std::string GTestAssert() const { return "ASSERT_EQ"; }

  virtual void GenerateAssertStatement(PrettyPrinter& printer, bool prepend_new_line) const {
    if (prepend_new_line) {
      printer << '\n';
    }

    std::string old_name = name();

    this->GenerateInitialization(printer, "_expected");

    printer << this->GTestAssert() << "(";
    this->GenerateName(printer);
    printer << ", ";
    this->GenerateName(printer, "_expected");
    printer << ");\n";
  }

 private:
  const std::string name_;
  const Value* const value_;
  const Type* const for_type_;
};

class CppVisitor : public Visitor {
 public:
  explicit CppVisitor(std::string_view name = "unnamed_value") : name_(name) {}

  std::shared_ptr<CppVariable> result() { return result_; }

  void VisitValue(const Value* node, const Type* for_type) override {
    std::shared_ptr<CppVariable> value = std::make_shared<CppVariable>(name_, node, for_type);
    result_ = std::move(value);
  }

 private:
  std::shared_ptr<CppVariable> result_;
  const std::string name_;
};

}  // namespace fidl_codec

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CPP_VISITOR_H_

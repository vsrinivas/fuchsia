// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEMPLATE_PARAMETER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEMPLATE_PARAMETER_H_

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// DWARF annotates template definitions with a sequence of template parameter definitions so the
// debugger can figure out the types of each template. These are encoded in the same order as the
// template definition in the source code.
class TemplateParameter : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted.

  // The template parameter name can be retrieved with GetAssignedName().

  // The type of this template parameter.
  const LazySymbol& type() const { return type_; }

  // The template parameters can either be types or values. We don't currently have a need for the
  // actual values so just encode that it was a value. The actual values could be added in the
  // future. In this example:
  //
  //   template<typename T, int i>
  //
  // The first parameter will be !is_value() with a name "T" and a type() of whatever it was
  // instantiated with. And the second will be is_valud() with a name "i", a type() "int", and
  // a value of whatever the value was.
  bool is_value() const { return is_value_; }

  // Symbol pubilc overrides:
  const TemplateParameter* AsTemplateParameter() const override { return this; }
  const std::string& GetAssignedName() const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(TemplateParameter);
  FRIEND_MAKE_REF_COUNTED(TemplateParameter);

  TemplateParameter(const std::string& name, LazySymbol type, bool is_value);
  ~TemplateParameter() override;

  // Symbol protected overrides.
  Identifier ComputeIdentifier() const override;

  std::string name_;
  LazySymbol type_;
  bool is_value_ = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEMPLATE_PARAMETER_H_

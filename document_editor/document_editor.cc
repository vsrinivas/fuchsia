// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/document_store/interfaces/document.mojom.h"

#include "apps/modular/document_editor/document_editor.h"

namespace modular {

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Property;
using document_store::PropertyPtr;
using document_store::Statement;
using document_store::StatementPtr;
using document_store::Value;
using document_store::ValuePtr;

using mojo::Array;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::String;
using mojo::StructPtr;

DocumentEditor::DocumentEditor() {
  doc_ = Document::New();
  doc_->properties = Array<PropertyPtr>::New(0);
}

DocumentEditor::DocumentEditor(const std::string& docid) : DocumentEditor() {
  doc_->docid = docid;
}

document_store::DocumentPtr DocumentEditor::TakeDocument() {
  return doc_.Pass();
}

Value* DocumentEditor::GetValue(std::string property) {
  for (auto& p : doc_->properties) {
    if (p->property == property) return p->value.get();
  }
  return nullptr;
}

void DocumentEditor::AddProperty(const std::string& property_val, StructPtr<Value> value) {
  auto property = document_store::Property::New();
  property->property = property_val;
  property->value = std::move(value);
  AddProperty(std::move(property));
}

ValuePtr DocumentEditor::NewIntValue(int64_t int_val) {
  ValuePtr value = Value::New();
  value->set_int_value(int_val);
  return value;
}

ValuePtr DocumentEditor::NewDoubleValue(double double_val) {
  ValuePtr value = Value::New();
  value->set_float_value(double_val);
  return value;
}

ValuePtr DocumentEditor::NewStringValue(const std::string& string_val) {
  ValuePtr value = Value::New();
  value->set_string_value(string_val);
  return value;
}

ValuePtr DocumentEditor::NewIriValue(const std::string& iri) {
  ValuePtr value = Value::New();
  value->set_iri(iri);
  return value;
}

std::string DocumentEditor::ToString(const DocumentPtr& doc) {
  std::ostringstream text;

  if (doc.is_null())
    return "\n  nullptr Document - possible zombie from std::move()";
  text << std::endl << "  @id: " << doc->docid;
  if (doc->properties.size() == 0) text << std::endl << "  (No properties)";
  for (const auto& prop : doc->properties) {
    text << std::endl << "  " << prop->property << ": ";
    Value* v = prop->value.get();
    switch (v->which()) {
      case Value::Tag::STRING_VALUE:
        text << v->get_string_value();
        break;
      case Value::Tag::INT_VALUE:
        text << v->get_int_value();
        break;
      case Value::Tag::FLOAT_VALUE:
        text << v->get_float_value();
        break;
      case Value::Tag::BINARY:
        text << "(binary)";
        break;
      case Value::Tag::IRI:
        text << v->get_iri();
        break;
      case Value::Tag::__UNKNOWN__:
        text << "(unknown)";
        break;
    }
  }
  return text.str();
}

StatementPtr NewStatement(const std::string& docid,
                          const std::string& property,
                          StructPtr<Value> value) {
  auto statement = document_store::Statement::New();
  statement->docid = docid;
  statement->property = property;
  statement->value = std::move(value);
  return statement;
}

}

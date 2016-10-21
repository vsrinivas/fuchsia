// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/document_editor/document_editor.h"

#include <iterator>
#include <ostream>
#include <vector>

#include "apps/document_store/interfaces/document.mojom.h"
#include "lib/ftl/logging.h"

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

bool DocumentEditor::TakeFromArray(const std::string& docid,
                                   Array<DocumentPtr>* array) {
  if (!*array) return false;

  std::vector<DocumentPtr> docs;
  array->Swap(&docs);
  auto it = std::remove_if(
      docs.begin(), docs.end(),
      [docid](const DocumentPtr& doc) { return doc->docid == docid; });

  bool result = false;
  if (it != docs.end()) {
    FTL_DCHECK(std::distance(it, docs.end()) == 1);
    doc_ = std::move(*it);
    docs.pop_back();
    result = true;
  }
  array->Swap(&docs);
  return result;
}

void DocumentEditor::SetProperty(PropertyPtr new_property) {
  for (auto& p : doc_->properties) {
    if (p->property == new_property->property) {
      // Note: it's valid for new_property->value to be null.
      std::swap(p, new_property);
      return;
    }
  }
  doc_->properties.push_back(std::move(new_property));
}

void DocumentEditor::SetProperty(const std::string& property_label,
                                 StructPtr<Value> value) {
  auto property = document_store::Property::New();
  property->property = property_label;
  property->value = std::move(value);
  SetProperty(std::move(property));
}

void DocumentEditor::RemoveProperty(const Property& del_property) {
  for (auto& p : doc_->properties) {
    if (p->Equals(del_property)) {
      int last = doc_->properties.size() - 1;
      std::swap(p, doc_->properties[last]);
      doc_->properties.resize(last);
      return;
    }
  }
}

void DocumentEditor::RemoveProperty(const std::string& property_label) {
  std::vector<PropertyPtr> vec;
  doc_->properties.Swap(&vec);
  auto it = std::remove_if(vec.begin(), vec.end(),
                           [&property_label](const PropertyPtr& p) {
                             return (p->property == property_label);
                           });
  vec.erase(it, vec.end());
  doc_->properties.Swap(&vec);
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

std::ostream& operator<<(std::ostream& os, const DocumentPtr& doc) {
  if (doc.is_null())
    return os << std::endl
              << "  nullptr Document - possible zombie from std::move()";
  os << std::endl << "  @id: " << doc->docid;
  if (doc->properties.size() == 0) os << std::endl << "  (No properties)";
  return os << std::dec << doc.get();
}

std::ostream& operator<<(std::ostream& os, Document* doc) {
  for (const auto& prop : doc->properties) {
    Value* v = prop->value.get();
    os << std::endl << "  " << prop->property << ": " << v;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, Value* v) {
  switch (v->which()) {
    case Value::Tag::STRING_VALUE:
      os << v->get_string_value();
      break;
    case Value::Tag::INT_VALUE:
      os << v->get_int_value();
      break;
    case Value::Tag::FLOAT_VALUE:
      os << v->get_float_value();
      break;
    case Value::Tag::BINARY:
      os << "(binary)";
      break;
    case Value::Tag::IRI:
      os << v->get_iri();
      break;
    case Value::Tag::__UNKNOWN__:
      os << "(unknown)";
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Array<DocumentPtr>& docs) {
  // Can't use range-based for because there is no const version of begin().
  if (docs.size() == 0) os << " NO DOCUMENTS";
  for (size_t i = 0; i < docs.size(); ++i) {
    if (i) {
      os << std::endl << "--------";
    }
    os << docs[i];
  }
  return os;
}

StatementPtr NewStatement(const std::string& docid, const std::string& property,
                          StructPtr<Value> value) {
  auto statement = document_store::Statement::New();
  statement->docid = docid;
  statement->property = property;
  statement->value = std::move(value);
  return statement;
}
}

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

using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::String;
using mojo::StructPtr;

DocumentEditor::DocumentEditor() {
}

DocumentEditor::DocumentEditor(const std::string& docid) {
  doc_ = Document::New();
  doc_->properties = MojoPropertyArray::New(0);
  doc_->docid = docid;
}

Value* DocumentEditor::GetValue(const std::string& property) {
  for (auto& p : doc_->properties) {
    if (p->property == property) return p->value.get();
  }
  return nullptr;
}

bool DocumentEditor::Edit(
    const std::string& docid, MojoDocMap* docs) {
  if (!*docs) return false;

  auto it = docs->find(docid);
  if (it == docs->end()) return false;

  doc_ = std::move(it.GetValue());

  DocMap doc_map;
  docs->Swap(&doc_map);
  doc_map.erase(docid);
  docs->Swap(&doc_map);

  return true;
}

void DocumentEditor::Keep(MojoDocMap* docs) {
  FTL_DCHECK(docs != nullptr);
  (*docs)[doc_->docid] = std::move(doc_);
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

std::ostream& operator<<(std::ostream& os, const MojoPropertyArray& props) {
  if (props.size() == 0) os << std::endl << "  (No properties)";
  // Cast away const because there's no "const" member function of begin() for
  // the range-based for loop, nor is there a cbegin().
  for (const auto& prop : const_cast<MojoPropertyArray&>(props)) {
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
    default:
      os << "(unimplemented value type)";
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const MojoDocMap& docs) {
  if (docs.size() == 0) os << " NO DOCUMENTS";
  for (auto it = docs.cbegin(); it != docs.cend(); ++it) {
    if (it != docs.cbegin()) {
      os << std::endl << "--------";
    }
    os << std::endl << "  @id: " << it.GetKey();
    if (it.GetValue()->properties.size() == 0) os << std::endl << "  (No properties)";
    return os << std::dec << it.GetValue()->properties;
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

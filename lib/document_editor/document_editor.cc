// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/document_editor/document_editor.h"

#include <iterator>
#include <ostream>
#include <vector>

#include "apps/modular/services/document_store/document.fidl.h"
#include "lib/ftl/logging.h"

namespace modular {

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Statement;
using document_store::StatementPtr;
using document_store::Value;
using document_store::ValuePtr;

using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;
using fidl::String;
using fidl::StructPtr;

DocumentEditor::DocumentEditor() {}

DocumentEditor::DocumentEditor(Document* const doc) : doc_(doc) {}

DocumentEditor::DocumentEditor(const std::string& docid) {
  newdoc_ = Document::New();
  newdoc_->docid = docid;
  doc_ = newdoc_.get();
  doc_->properties.mark_non_null();
}

void DocumentEditor::Reset() {
  doc_ = nullptr;
  newdoc_.reset();
}

Value* DocumentEditor::GetValue(const std::string& property) {
  auto p = doc_->properties.find(property);
  if (p == doc_->properties.end())
    return nullptr;

  return p.GetValue().get();
}

bool DocumentEditor::Edit(const std::string& docid, FidlDocMap* const docs) {
  FTL_DCHECK(newdoc_.get() == nullptr);
  FTL_DCHECK(doc_ == nullptr);
  FTL_DCHECK(docs != nullptr);

  auto it = docs->find(docid);
  if (it == docs->end())
    return false;

  doc_ = it.GetValue().get();

  return true;
}

void DocumentEditor::TakeDocument(document_store::DocumentPtr* const ptr) {
  FTL_DCHECK(newdoc_.get() != nullptr);
  *ptr = std::move(newdoc_);
}

void DocumentEditor::Insert(FidlDocMap* const docs) {
  FTL_DCHECK(newdoc_.get() != nullptr);
  FTL_DCHECK(docs != nullptr);
  (*docs)[newdoc_->docid] = std::move(newdoc_);
}

DocumentEditor& DocumentEditor::SetProperty(const std::string& property_label,
                                            StructPtr<Value> value) {
  doc_->properties[property_label] = std::move(value);
  return *this;
}

void DocumentEditor::RemoveProperty(const std::string& property_label) {
  std::map<FidlPropertyMap::KeyType, FidlPropertyMap::ValueType> props;
  doc_->properties.Swap(&props);
  props.erase(property_label);
  doc_->properties.Swap(&props);
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

std::ostream& operator<<(std::ostream& os, const FidlPropertyMap& props) {
  if (props.size() == 0)
    os << std::endl << "  (No properties)";
  for (auto it = props.cbegin(); it != props.cend(); ++it) {
    Value* v = it.GetValue().get();
    os << std::endl << "  " << it.GetKey() << ": " << v;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, Value* const v) {
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

std::ostream& operator<<(std::ostream& os, const FidlDocMap& docs) {
  if (docs.size() == 0)
    os << " NO DOCUMENTS";
  for (auto it = docs.cbegin(); it != docs.cend(); ++it) {
    if (it != docs.cbegin()) {
      os << std::endl << "--------";
    }
    os << std::endl << "  @id: " << it.GetKey();
    if (it.GetValue()->properties.size() == 0)
      os << std::endl << "  (No properties)";
    return os << std::dec << it.GetValue()->properties;
  }
  return os;
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

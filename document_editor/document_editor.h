// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_APPS_MODULAR_DOCUMENT_EDITOR_DOCUMENT_EDITOR_H__
#define MOJO_APPS_MODULAR_DOCUMENT_EDITOR_DOCUMENT_EDITOR_H__

#include <map>

#include "apps/document_store/interfaces/document.mojom.h"
#include "lib/ftl/macros.h"

namespace modular {

using MojoDocMap =
    mojo::Map<mojo::String, document_store::DocumentPtr>;
using DocMap = std::map<mojo::String, document_store::DocumentPtr>;
using MojoPropertyArray = mojo::Array<document_store::PropertyPtr>;

// Wrapper class to make it easier to work with Document objects and all
// of the nested StructPtr's. This class will evolve to support:
//   - more operations on documents, such as removing properties.
//   - arrays, sets, and other XML data types.
//   - more functions to support Link objects, such as diff.
class DocumentEditor {
 public:
  // Construct a new, empty document. This constructor should be used with the
  // Edit() function.
  DocumentEditor();

  // Construct a new document with the given document id, but no properties.
  DocumentEditor(const std::string& docid);

  const mojo::String& docid() { return this->doc_->docid; }

  void TakeDocument(document_store::DocumentPtr* ptr) {
    *ptr = std::move(doc_);
  }

  // Return the given document, or null if there isn't one.
  // Remove the given document from the map and prepare to edit it.
  bool Edit(const std::string& docid, MojoDocMap* docs);

  // Return the current document to the document map. Inverse of the Edit()
  // function.
  void Keep(MojoDocMap* docs);

  // Return the value for the given property, or nullptr if not found.
  // The result points directly into the property array and may be modified.
  document_store::Value* GetValue(const std::string& property);

  // Add the given property to the Document. Duplicates are currently not
  // ignored. Multiple properties of the same property name are theoretically
  // allowed, but are not currently handled.
  void SetProperty(document_store::PropertyPtr property);

  void SetProperty(const std::string& property_label,
                   document_store::ValuePtr value);

  // Remove the given label/value from the Document. Both the property name
  // and the value must be matched, otherwise do nothing.
  // The Document may have no properties when this function completes.
  void RemoveProperty(const document_store::Property& property);

  // Remove all instances of the given property from the Document.
  // Do nothing if there property is not found.
  // The Document may have no properties when this function completes.
  void RemoveProperty(const std::string& property_label);

  // Create a new ValuePtr for an int64_t.
  static document_store::ValuePtr NewIntValue(int64_t int_val);

  // Create a new ValuePtr for a double.
  static document_store::ValuePtr NewDoubleValue(double double_val);

  // Create a new ValuePtr for a std::string
  static document_store::ValuePtr NewStringValue(const std::string& string_val);

  // Create a new ValuePtr for a std::string that is an IRI
  static document_store::ValuePtr NewIriValue(const std::string& string_val);

  // Create a text string of all properties, appropriate for debugging.
  static std::string ToString(const document_store::DocumentPtr& doc);

 private:
  document_store::DocumentPtr doc_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DocumentEditor);
};

std::ostream& operator<<(std::ostream& os, const MojoDocMap& docs);
std::ostream& operator<<(std::ostream& os,
                         const document_store::DocumentPtr& doc);
std::ostream& operator<<(std::ostream& os, document_store::Value* v);

// Create a StatementPtr based on the given triple.
mojo::StructPtr<document_store::Statement> NewStatement(
    const std::string& docid, const std::string& property,
    mojo::StructPtr<document_store::Value> value);
}

#endif  // MOJO_APPS_MODULAR_DOCUMENT_EDITOR_DOCUMENT_EDITOR_H__

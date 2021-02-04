// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_CONVERSION_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_CONVERSION_H_

// A Conversion is an object that applies a specific translation from one syntax
// to another.  Conversions may nest other conversions, such that they may use
// the output of the conversion of their nested children when creating their own
// output.
#include <string>
#include <optional>

#include "raw_ast.h"

namespace fidl::conv {

// CopyRange is very similar to SourceElement, except that it does not need to
// map from the source file text to a syntax tree node exactly.  Instead, it
// merely specifies the span between two "convertible" portions of the source
// file.
class CopyRange {
 public:
  CopyRange(
      const char* from,
      const char* until)
      : from_(from),
        until_(until) {}

  const char* from_;
  const char* until_;
};

class Conversion {
 public:
  Conversion() : copy_range_(nullptr) {}
  virtual ~Conversion() = default;

  // An enumeration of supported syntaxes.  There are currently two available:
  // kNew is the "new" syntax, while kOld is the "valid" FIDL syntax as of
  // Jan 1, 2021.
  enum struct Syntax {
    kOld,
    kNew,
  };

  // Some conversions start with a span of text that can be copied character for
  // character.  For example, consider the following const declaration, written
  // in the old syntax.
  //
  //  const uint8 FOO = 5;
  // <--A--|----B----|-C-->
  //
  // Span B is the portion of text being converted (done in this case via a
  // NameAndTypeConversion).  Spans A and C do not need to be converted, and can
  // be copied verbatim.  The CopyRange describing SpanA would thus be passed to
  // the AddPrefix() method of the NameAndTypeConversion, while C would be
  // included in the prefix of whatever conversion comes next.
  void AddPrefix(std::unique_ptr<CopyRange> copy_range) {
    copy_range_ = std::move(copy_range);
  }

  // A conversion that nests other conversions inside of makes this method
  // availables in order to ingest the results of those operations.  For
  // example, consider the following alias declaration, written in the old
  // syntax:
  //
  //  alias my_type = array<handle:<PORT,7>?>:5;
  //                       |--------A-------|
  //                 |------------B------------|
  //
  // Conversion A (for the "handle" type declaration) and is nested within
  // conversion B (for "array").  When the inner conversion is resolved and
  // stringified via its Write() method (to "handle:<optional,PORT,7>" in this
  // case), its result must be passed up the Conversion object handling the
  // outer conversion, which will use it like "array<[CONV_A_OUTPUT],5>"
  virtual void AddChildText(std::string child) = 0;

  // Write produces a string of converted text, and contains the logic for
  // taking the SourceElement of the node being converted, along with any child
  // text that has been attached, and creating the converted output.
  virtual std::string Write(Syntax syntax) = 0;

 protected:
  std::string prefix() {
    if (copy_range_ != nullptr) {
      return std::string(copy_range_->from_, copy_range_->until_);
    }
    return "";
  }

 private:
  std::unique_ptr<CopyRange> copy_range_;
};

// TypeConversion encapsulates the complex logic for converting various type
// definitions from the old syntax to the new.  It may nest other
// TypeConversions, as would be the case for something like "vector<handle?>."
class TypeConversion : public Conversion {
 public:
  explicit TypeConversion(
      const std::unique_ptr<raw::TypeConstructor>& type_ctor)
      : type_ctor_(type_ctor) {}
  ~TypeConversion() override = default;

  const std::unique_ptr<raw::TypeConstructor>& type_ctor_;
  std::string wrapped_type_text_;

  void AddChildText(std::string child) override {
    wrapped_type_text_ = child;
  }

  std::string Write(Syntax syntax) override;
};

// Handles the application of the "types come second" rule specified by FTP-050.
// For example, this is the conversion used to turn "uint8 FOO" into "FOO
// uint8."  The NameAndTypeConversion always nests a TypeConversion.
class NameAndTypeConversion : public Conversion {
 public:
  NameAndTypeConversion(
      const std::unique_ptr<raw::Identifier>& identifier,
      const std::unique_ptr<raw::TypeConstructor>& type_ctor)
      : identifier_(identifier),
        type_ctor_(type_ctor) {}
  ~NameAndTypeConversion() override = default;

  const std::unique_ptr<raw::Identifier>& identifier_;
  const std::unique_ptr<raw::TypeConstructor>& type_ctor_;
  std::string type_text_;

  void AddChildText(std::string child) override {
    type_text_ = child;
  }

  std::string Write(Syntax syntax) override;
};

// An abstract class for the conversion of "membered" types, ie types that may
// have an arbitrary number of members defined in a "{...}" block.  Examples of
// such types include protocol, struct, table, union, etc.
//
// All such types have three common properties: they may or may not specify
// "resourceness," they may or may not specify "strictness," and they must have
// one ore more member types declared in their "{...}" block.
class MemberedDeclarationConversion : public Conversion {
 public:
  MemberedDeclarationConversion(
      const std::unique_ptr<raw::Identifier>& identifier,
      const std::optional<types::Strictness>& strictness,
      const types::Resourceness& resourceness)
      : identifier_(identifier),
        strictness_(strictness),
        resourceness_(resourceness) {}
  ~MemberedDeclarationConversion() override = default;

  const std::unique_ptr<raw::Identifier>& identifier_;
  const std::optional<types::Strictness> strictness_;
  const types::Resourceness resourceness_;
  std::vector<std::string> members_;

  void AddChildText(std::string child) override {
    members_.push_back(child);
  }

  std::string Write(Syntax syntax) override;

 private:
  virtual std::string get_fidl_type() = 0;

  std::string get_decl_str() {
    std::string decl;
    if (resourceness_ == types::Resourceness::kResource) {
      decl += "resource ";
    }
    if (strictness_ != std::nullopt) {
      if (strictness_.value() == types::Strictness::kStrict) {
        decl += "strict ";
      } else if (strictness_.value() == types::Strictness::kFlexible) {
        decl += "flexible ";
      }
    }
    return decl + get_fidl_type();
  }
};

// Handles the conversion of a struct declaration, written in the old syntax as:
//
// [resource ][strict|flexible ] struct S {...}
//
// The individual struct member conversions are meant to be nested within this
// one as NameAndTypeConversions using the AddChildText() method.
class StructDeclarationConversion : public MemberedDeclarationConversion {
 public:
  StructDeclarationConversion(
      const std::unique_ptr<raw::Identifier>& identifier,
      const types::Resourceness& resourceness)
      : MemberedDeclarationConversion(identifier, std::nullopt, resourceness) {}

 private:
  std::string get_fidl_type() override {
    return "struct";
  }
};

}  // namespace fidl::conv

#endif //ZIRCON_TOOLS_FIDL_INCLUDE_CONVERSION_H_

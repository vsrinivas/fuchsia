// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_SYNTAX_CONVERSION_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_SYNTAX_CONVERSION_H_

// A Conversion is an object that applies a specific translation from one syntax
// to another.  Conversions may nest other conversions, such that they may use
// the output of the conversion of their nested children when creating their own
// output.
#include <optional>
#include <string>

#include "raw_ast.h"
#include "underlying_type.h"
#include "utils.h"

namespace fidl::conv {

// CopyRange is very similar to SourceElement, except that it does not need to
// map from the source file text to a syntax tree node exactly.  Instead, it
// merely specifies the span between two "convertible" portions of the source
// file.
class CopyRange {
 public:
  CopyRange(const char* from, const char* until) : from_(from), until_(until) {}

  const char* from_;
  const char* until_;
};

class Conversion {
 public:
  Conversion() = default;
  virtual ~Conversion() = default;

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
    copy_ranges_.emplace_back(std::move(copy_range));
  }

  // A conversion that nests other conversions inside of itself enables this
  // method in order to ingest the results of those child conversions.  For
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
  virtual std::string Write(fidl::utils::Syntax syntax) = 0;

 protected:
  std::string prefix() {
    std::string out;
    for (auto& copy_range : copy_ranges_) {
      out += std::string(copy_range->from_, copy_range->until_);
    }
    return out;
  }

 private:
  std::vector<std::unique_ptr<CopyRange>> copy_ranges_;
};

// A conversion that leaves its contents exactly as it found them.  This is
// useful for cases where a span is only converted in certain cases.
class NoopConversion : public Conversion {
 public:
  explicit NoopConversion(const Token& start, const Token& end) : start_(start), end_(end) {}
  ~NoopConversion() override = default;

  const Token& start_;
  const Token& end_;

  void AddChildText(std::string child) override {}

  std::string Write(fidl::utils::Syntax syntax) override {
    const char* from = start_.data().data();
    const char* until = end_.data().data() + end_.data().length();
    return prefix() + std::string(from, until);
  }
};

// Converts a single attribute, one of potentially several in an AttributeList.
class AttributeConversion : public Conversion {
 public:
  AttributeConversion(const std::string& name,
                      const std::optional<std::reference_wrapper<const raw::StringLiteral>> value)
      : name_(name), value_(value) {}
  ~AttributeConversion() override = default;

  const std::string& name_;
  const std::optional<std::reference_wrapper<const raw::StringLiteral>> value_;

  void AddChildText(std::string child) override {}

  std::string Write(fidl::utils::Syntax syntax) override;
};

// Handles an AttributeList.  Such lists have one peculiarity to be aware of,
// which is the special handling they require when they include doc comments.
// Unlike regular attributes, doc comments need not be converted, and should not
// appear in the bracketed attribute list.  Because such comments are always
// first in the AttributeList if they exist, we can just check if the first
// comment is a doc comment, and special case its conversion.
class AttributeListConversion : public Conversion {
 public:
  explicit AttributeListConversion(bool has_doc_comment) : has_doc_comment_(has_doc_comment) {}
  ~AttributeListConversion() override = default;

  std::vector<std::string> attributes_;
  bool has_doc_comment_;

  void AddChildText(std::string child) override { attributes_.push_back(child); }

  std::string Write(fidl::utils::Syntax syntax) override;
};

// TypeConversion encapsulates the complex logic for converting various type
// definitions from the old syntax to the new.  It may nest other
// TypeConversions, as would be the case for something like "vector<handle?>."
class TypeConversion : public Conversion {
 public:
  explicit TypeConversion(const std::unique_ptr<raw::TypeConstructorOld>& type_ctor,
                          const UnderlyingType underlying_type)
      : type_ctor_(type_ctor), underlying_type_(underlying_type) {}
  ~TypeConversion() override = default;

  const std::unique_ptr<raw::TypeConstructorOld>& type_ctor_;
  const UnderlyingType underlying_type_;
  std::string wrapped_type_text_;

  void AddChildText(std::string child) override { wrapped_type_text_ = child; }

  std::string Write(fidl::utils::Syntax syntax) override;
};

// Handles the application of the "types come second" rule specified by FTP-050.
// For example, this is the conversion used to turn "uint8 FOO" into "FOO
// uint8."  The NameAndTypeConversion always nests a TypeConversion.
class NameAndTypeConversion : public Conversion {
 public:
  NameAndTypeConversion(const std::unique_ptr<raw::Identifier>& identifier,
                        const std::unique_ptr<raw::TypeConstructorOld>& type_ctor)
      : identifier_(identifier), type_ctor_(type_ctor) {}
  ~NameAndTypeConversion() override = default;

  const std::unique_ptr<raw::Identifier>& identifier_;
  const std::unique_ptr<raw::TypeConstructorOld>& type_ctor_;
  std::string type_text_;

  void AddChildText(std::string child) override { type_text_ = child; }

  std::string Write(fidl::utils::Syntax syntax) override;
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
  MemberedDeclarationConversion(const std::unique_ptr<raw::Identifier>& identifier,
                                const types::Resourceness& resourceness)
      : identifier_(identifier), resourceness_(resourceness) {}
  ~MemberedDeclarationConversion() override = default;

  const std::unique_ptr<raw::Identifier>& identifier_;
  const types::Resourceness resourceness_;
  std::vector<std::string> members_;

  void AddChildText(std::string child) override { members_.push_back(child); }

  std::string Write(fidl::utils::Syntax syntax) override;

 protected:
  virtual std::string get_fidl_type() = 0;
  virtual std::string get_modifiers(fidl::utils::Syntax syntax) {
    return resourceness_ == types::Resourceness::kResource ? "resource " : "";
  }

  std::string get_decl_str(fidl::utils::Syntax syntax) {
    return get_modifiers(syntax) + get_fidl_type();
  }
};

class FlexibleTypeConversion : public MemberedDeclarationConversion {
 public:
  FlexibleTypeConversion(const std::unique_ptr<raw::Identifier>& identifier,
                         const types::Resourceness& resourceness,
                         const std::optional<types::Strictness>& strictness)
      : MemberedDeclarationConversion(identifier, resourceness), strictness_(strictness) {}

  // this represents the modifier specified in source rather than the actual
  // underlying strictness of the type, which is why the optional is required
  // to represent the state of "no strictness specified".
  std::optional<types::Strictness> strictness_;

 protected:
  // There is a small inconsistency: converting back to the old syntax always
  // orders resourceness after strictness, even if the original declaration was
  // in the reverse order.  In other words, for old syntax printing, `resource
  // flexible union` gets reprinted as `flexible resource union`. This only
  // occurs for `union`, as it is the only declaration that can carry both
  // modifiers.  This oversight is okay for the purposes of fidlconv.
  std::string get_modifiers(fidl::utils::Syntax syntax) override {
    std::string modifiers;
    if (syntax == fidl::utils::Syntax::kNew) {
      modifiers += ((strictness_.value_or(types::Strictness::kStrict) == types::Strictness::kStrict)
                        ? "strict "
                        : "flexible ");
    } else if (strictness_ != std::nullopt) {
      if (strictness_.value() == types::Strictness::kStrict) {
        modifiers += "strict ";
      } else if (strictness_.value() == types::Strictness::kFlexible) {
        modifiers += "flexible ";
      }
    }
    return modifiers + MemberedDeclarationConversion::get_modifiers(syntax);
  }
};

// Handles the conversion of a struct declaration, written in the old syntax as:
//
// [resource] struct S {...}
//
// The individual struct member conversions are meant to be nested within this
// one as NameAndTypeConversions using the AddChildText() method.
class StructDeclarationConversion : public MemberedDeclarationConversion {
 public:
  StructDeclarationConversion(const std::unique_ptr<raw::Identifier>& identifier,
                              const types::Resourceness& resourceness)
      : MemberedDeclarationConversion(identifier, resourceness) {}

 private:
  std::string get_fidl_type() override { return "struct"; }
};

// Handles the conversion of a table declaration, written in the old syntax as:
//
// [resource] table T {...}
//
// The individual table member conversions are meant to be nested within this
// one as NameAndTypeConversions using the AddChildText() method.
class TableDeclarationConversion : public MemberedDeclarationConversion {
 public:
  TableDeclarationConversion(const std::unique_ptr<raw::Identifier>& identifier,
                             const types::Resourceness& resourceness)
      : MemberedDeclarationConversion(identifier, resourceness) {}

 private:
  std::string get_fidl_type() override { return "table"; }
};

// Handles the conversion of a union declaration, written in the old syntax as:
//
// [resource ][flexible|strict] union U {...}
//
// The individual union member conversions are meant to be nested within this
// one as NameAndTypeConversions using the AddChildText() method.
class UnionDeclarationConversion : public FlexibleTypeConversion {
 public:
  UnionDeclarationConversion(const std::unique_ptr<raw::Identifier>& identifier,
                             const std::optional<types::Strictness>& strictness,
                             const types::Resourceness& resourceness)
      : FlexibleTypeConversion(identifier, resourceness, strictness) {}

 private:
  std::string get_fidl_type() override { return "union"; }
};

// Handles the conversion of declarations specified using the bits keyword.
// It is similar to the MemberedDeclarationConversion that it wraps, but has to
// account for the possibility that a declaration contains an (optional)
// wrapped type, like "bits NAME : WRAPPED_TYPE {..."
class BitsDeclarationConversion : public FlexibleTypeConversion {
 public:
  BitsDeclarationConversion(
      const std::unique_ptr<raw::Identifier>& identifier,
      const std::optional<std::reference_wrapper<std::unique_ptr<raw::TypeConstructorOld>>>&
          maybe_wrapped_type,
      const std::optional<types::Strictness>& strictness)
      : FlexibleTypeConversion(identifier, types::Resourceness::kValue, strictness),
        maybe_wrapped_type_(maybe_wrapped_type) {}

  const std::optional<std::reference_wrapper<std::unique_ptr<raw::TypeConstructorOld>>>
      maybe_wrapped_type_;

  void AddChildText(std::string child) override { members_.push_back(child); }

  std::string Write(fidl::utils::Syntax syntax) override;

 private:
  std::string get_fidl_type() override { return "bits"; }

  std::string get_wrapped_type() {
    if (maybe_wrapped_type_.has_value()) {
      return " : " + maybe_wrapped_type_.value().get()->copy_to_str();
    }
    return "";
  }
};

// Identical to the BitsDeclarationConversion, except that it replaces the word
// "bits" with "enum."
class EnumDeclarationConversion : public BitsDeclarationConversion {
 public:
  EnumDeclarationConversion(
      const std::unique_ptr<raw::Identifier>& identifier,
      const std::optional<std::reference_wrapper<std::unique_ptr<raw::TypeConstructorOld>>>&
          maybe_wrapped_type,
      const std::optional<types::Strictness>& strictness)
      : BitsDeclarationConversion(identifier, maybe_wrapped_type, strictness) {}

 private:
  std::string get_fidl_type() override { return "enum"; }
};

}  // namespace fidl::conv

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_SYNTAX_CONVERSION_H_

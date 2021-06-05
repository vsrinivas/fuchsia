// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SPAN_SEQUENCE_TREE_VISITOR_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SPAN_SEQUENCE_TREE_VISITOR_H_

#include <stack>

#include "raw_ast.h"
#include "span_sequence.h"
#include "tree_visitor.h"

namespace fidl::fmt {

// This class is a pretty printer for a parse-able FIDL file.  It takes two representations of the
// file as input: the raw AST (via the OnFile method), and a view into the source text of the file
// from which that raw AST was generated.
class SpanSequenceTreeVisitor : public raw::DeclarationOrderTreeVisitor {
 public:
  explicit SpanSequenceTreeVisitor(std::string_view file)
      : empty_lines_(0), file_(file), uningested_(file) {
    building_.push(std::vector<std::unique_ptr<SpanSequence>>());
  }

  // An RAII-ed base class for constructing SpanSequence's from inside On* visitor methods.  Each
  // instance of a Builder is roughly saying "make a SpanSequence out of text between the end of the
  // last processed node and the one currently being visited."
  template <typename T>
  class Builder {
    static_assert(std::is_base_of<SpanSequence, T>::value,
                  "T of Builder<T> must inherit from SpanSequence");

   public:
    Builder(SpanSequenceTreeVisitor* ftv, const raw::SourceElement& element);
    virtual ~Builder();

   protected:
    SpanSequenceTreeVisitor* GetFormattingTreeVisitor() { return ftv_; }

   private:
    SpanSequenceTreeVisitor* ftv_;
    const raw::SourceElement& el_;
  };

  // Builds a single TokenSpanSequence.  For example, consider the following FIDL:
  //
  //   // My standalone comment.
  //   using foo.bar as qux; // My inline comment.
  //
  // All three of `foo`, `baz,` and `qux` will be visited by the OnIdentifier method.  Each instance
  // of this method will instantiate a TokenBuilder, as entire span covered by an Identifier node
  // consists of a single token.
  class TokenBuilder : public Builder<TokenSpanSequence> {
   public:
    TokenBuilder(SpanSequenceTreeVisitor* ftv, const raw::SourceElement& element);
  };

  // Builds a CompositeSpanSequence that is smaller than a standalone statement (see the comment on
  // StatementBuilder for more on what that means), but still contains multiple tokens.  Using the
  // same example as above:
  //
  //   // My standalone comment.
  //   using foo.bar as qux; // My inline comment.
  //
  // The span `foo.bar` is a raw::CompoundIdentifier consisting of multiple tokens (`foo`, `.`, and
  // `bar`).  Since this span is not meant to be divisible, it should be constructed by a
  // SpanBuilder<AtomicSpanSequence>.  In contrast, a sub-statement length span that IS meant to be
  // divisible, like `@attr(foo="bar)`, should be constructed by SpanBuilder<DivisibleSpanSequence>
  // instead.
  template <typename T>
  class SpanBuilder : public Builder<T> {
    static_assert(std::is_base_of<CompositeSpanSequence, T>::value,
                  "T of SpanBuilder<T> must inherit from CompositeSpanSequence");

   public:
    SpanBuilder(SpanSequenceTreeVisitor* ftv, const raw::SourceElement& element,
                SpanSequence::Position position = SpanSequence::Position::kDefault);
    ~SpanBuilder();

   protected:
    SpanSequence::Position GetPosition() const { return position_; }

   private:
    const SpanSequence::Position position_;
  };

  // Builds a SpanSequence to represent a FIDL statement (ie any chain of tokens that ends in a
  // semicolon).  As illustration, both the protocol and method declarations here are statements,
  // one wrapping the other:
  //
  //   protocol {
  //     DoFoo(MyRequest) -> (MyResponse) error uint32;
  //   };
  //
  // The purpose of this Builder is to make a SpanSequence from all text from the end of the last
  // statement, up to and including the semicolon that ends this statement (as well as any inline
  // comments that may follow that semicolon).  Again taking the 'using...' example, the entirety of
  // the text below would become a single SpanSequence when passed through
  // StatementBuilder<AtomicSpanSequence>:
  //
  //   // My standalone comment.
  //   using foo.bar as qux; // My inline comment.
  //
  // For the `protocol...` example, `protocol ...` would be processed by
  // StatementBuilder<MultilineSpanSequence> (since protocols are multiline by default), whereas
  // `DoFoo...` would be handled by StatementBuilder<DivisibleSpanSequence> instead.
  template <typename T>
  class StatementBuilder : public SpanBuilder<T> {
   public:
    StatementBuilder(SpanSequenceTreeVisitor* ftv, const raw::SourceElement& element,
                     const Token& start,
                     SpanSequence::Position position = SpanSequence::Position::kDefault);
    ~StatementBuilder();

   private:
    const Token& start_;
    size_t statement_new_lines_;
  };

  // These "On*" methods may be called on files written in the new syntax.
  void OnAttributeListNew(std::unique_ptr<raw::AttributeListNew> const& element) override {
    NotYetImplemented();
  };
  void OnBinaryOperatorConstant(
      std::unique_ptr<raw::BinaryOperatorConstant> const& element) override {
    NotYetImplemented();
  };
  void OnComposeProtocol(std::unique_ptr<raw::ComposeProtocol> const& element) override {
    NotYetImplemented();
  };
  void OnCompoundIdentifier(std::unique_ptr<raw::CompoundIdentifier> const& element) override;
  void OnConstant(std::unique_ptr<raw::Constant> const& element) override { NotYetImplemented(); };
  void OnConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const& element) override {
    NotYetImplemented();
  }
  void OnFile(std::unique_ptr<raw::File> const& element) override;
  void OnIdentifier(std::unique_ptr<raw::Identifier> const& element) override;
  void OnIdentifierConstant(std::unique_ptr<raw::IdentifierConstant> const& element) override {
    NotYetImplemented();
  };
  void OnIdentifierLayoutParameter(
      std::unique_ptr<raw::IdentifierLayoutParameter> const& element) override {
    NotYetImplemented();
  }
  void OnInlineLayoutReference(
      std::unique_ptr<raw::InlineLayoutReference> const& element) override {
    NotYetImplemented();
  }
  void OnLayout(std::unique_ptr<raw::Layout> const& element) override { NotYetImplemented(); }
  void OnLayoutMember(std::unique_ptr<raw::LayoutMember> const& element) override {
    NotYetImplemented();
  }
  void OnLayoutParameter(std::unique_ptr<raw::LayoutParameter> const& element) override {
    NotYetImplemented();
  }
  void OnLayoutParameterList(std::unique_ptr<raw::LayoutParameterList> const& element) override {
    NotYetImplemented();
  }
  void OnLayoutReference(std::unique_ptr<raw::LayoutReference> const& element) override {
    NotYetImplemented();
  }
  void OnLibraryDecl(std::unique_ptr<raw::LibraryDecl> const& element) override;
  void OnLiteral(std::unique_ptr<raw::Literal> const& element) override { NotYetImplemented(); };
  void OnLiteralConstant(std::unique_ptr<raw::LiteralConstant> const& element) override {
    NotYetImplemented();
  };
  void OnLiteralLayoutParameter(
      std::unique_ptr<raw::LiteralLayoutParameter> const& element) override {
    NotYetImplemented();
  }
  void OnNamedLayoutReference(std::unique_ptr<raw::NamedLayoutReference> const& element) override {
    NotYetImplemented();
  }
  void OnNullability(types::Nullability nullability) override { NotYetImplemented(); };
  void OnOrdinaledLayoutMember(
      std::unique_ptr<raw::OrdinaledLayoutMember> const& element) override {
    NotYetImplemented();
  }
  void OnParameter(std::unique_ptr<raw::Parameter> const& element) override { NotYetImplemented(); }
  void OnParameterListNew(std::unique_ptr<raw::ParameterListNew> const& element) override {
    NotYetImplemented();
  }
  void OnPrimitiveSubtype(types::PrimitiveSubtype subtype) override { NotYetImplemented(); };
  void OnProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> const& element) override {
    NotYetImplemented();
  }
  void OnProtocolMethod(std::unique_ptr<raw::ProtocolMethod> const& element) override {
    NotYetImplemented();
  }
  void OnResourceDeclaration(std::unique_ptr<raw::ResourceDeclaration> const& element) override {
    NotYetImplemented();
  }
  void OnResourceProperty(std::unique_ptr<raw::ResourceProperty> const& element) override {
    NotYetImplemented();
  }
  void OnServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> const& element) override {
    NotYetImplemented();
  }
  void OnServiceMember(std::unique_ptr<raw::ServiceMember> const& element) override {
    NotYetImplemented();
  }
  void OnStructLayoutMember(std::unique_ptr<raw::StructLayoutMember> const& element) override {
    NotYetImplemented();
  }
  void OnTypeConstraints(std::unique_ptr<raw::TypeConstraints> const& element) override {
    NotYetImplemented();
  }
  void OnTypeConstructorNew(std::unique_ptr<raw::TypeConstructorNew> const& element) override {
    NotYetImplemented();
  }
  void OnTypeDecl(std::unique_ptr<raw::TypeDecl> const& element) override { NotYetImplemented(); }
  void OnTypeLayoutParameter(std::unique_ptr<raw::TypeLayoutParameter> const& element) override {
    NotYetImplemented();
  }
  void OnUsing(std::unique_ptr<raw::Using> const& element) override;
  void OnValueLayoutMember(std::unique_ptr<raw::ValueLayoutMember> const& element) override {
    NotYetImplemented();
  }

  // The remaining "On*" methods are all untouched by the new syntax, and should never be used by
  // this formatter.
  void OnAttributeOld(const raw::AttributeOld& element) override { AbortUnimplemented(); }
  void OnAttributeListOld(std::unique_ptr<raw::AttributeListOld> const& element) override {
    AbortUnimplemented();
  }
  void OnBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> const& element) override {
    AbortUnimplemented();
  }
  void OnBitsMember(std::unique_ptr<raw::BitsMember> const& element) override {
    AbortUnimplemented();
  }
  void OnEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> const& element) override {
    AbortUnimplemented();
  }
  void OnEnumMember(std::unique_ptr<raw::EnumMember> const& element) override {
    AbortUnimplemented();
  }
  void OnParameterListOld(std::unique_ptr<raw::ParameterListOld> const& element) override {
    AbortUnimplemented();
  }
  void OnStructDeclaration(std::unique_ptr<raw::StructDeclaration> const& element) override {
    AbortUnimplemented();
  }
  void OnStructMember(std::unique_ptr<raw::StructMember> const& element) override {
    AbortUnimplemented();
  }
  void OnTableDeclaration(std::unique_ptr<raw::TableDeclaration> const& element) override {
    AbortUnimplemented();
  }
  void OnTableMember(std::unique_ptr<raw::TableMember> const& element) override {
    AbortUnimplemented();
  }
  void OnTypeConstructorOld(std::unique_ptr<raw::TypeConstructorOld> const& element) override {
    AbortUnimplemented();
  }
  void OnUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> const& element) override {
    AbortUnimplemented();
  }
  void OnUnionMember(std::unique_ptr<raw::UnionMember> const& element) override {
    AbortUnimplemented();
  }

  // Must be called after OnFile() has been called.  Returns the result of the file fragmentation
  // work done by this class.
  MultilineSpanSequence Result();

 private:
  // Given a pointer to character in our source file, ingest up to and including that character.  If
  // the stop_at_semicolon flag is set, only ingest until we reach the end of a line containing a
  // semicolon, rather than going all the way up to the limit character.
  //
  // Generally, the best way to get the limit pointer is to call .data() on whatever the next Token
  // in the raw AST is.  All of the usual caveats when working with raw char arrays apply: make sure
  // that the limit character is actually in the range of the uningested_ string_view before
  // calling this function, make sure the limit pointer is not null, etc.
  std::optional<std::unique_ptr<SpanSequence>> IngestUntil(const char* limit,
                                                           bool stop_at_semicolon = false);
  std::optional<std::unique_ptr<SpanSequence>> IngestUntilEndOfFile();

  // Ingest until the first semicolon we encounter, taking care to include any inline comments that
  // may be trailing after that semicolon.  In other words, if we call this method on a string_view
  // that looks like `foo;\n` or `foo; bar`, we should expect to ingest the `foo;` portion.  But if
  // we call it on `foo; // bar\n`, we should expect to ingest the entire thing, trailing comment
  // included.
  //
  // This call is just sugar around IngestUntil(LAST_CHAR_OF_FILE, true);
  std::optional<std::unique_ptr<SpanSequence>> IngestUntilSemicolon();

  // A stack that keeps track of the CompositeSpanSequence we are currently building.  It is a list
  // of that CompositeSpanSequence's children.  When the child list has been filled out, it is
  // popped off the stack and pushed onto the new top element as its child.
  //
  // When this class is constructed, one element is added to this stack, serving as the "root"
  // SpanSequence for the file.  Calling this class' Result() method pops that element off and
  // returns it, representing the fully processed SpanSequence tree for the given source file, and
  // exhausting this class.
  std::stack<std::vector<std::unique_ptr<SpanSequence>>> building_;

  // Keeps track of the number of empty lines immediately preceding the uningested_ string_view in
  // the source file.  This value is used to set the leading_blank_lines value on the next
  // SpanSequence to be generated.
  size_t empty_lines_;

  // A view into the entire source file being formatted.  Unlike uningested_ below, this serves only
  // as a static reference.
  const std::string_view file_;

  // A view tracking the remaining portion of the file source string that has yet to be ingested by
  // the formatter.
  std::string_view uningested_;

  void static NotYetImplemented() {
    assert(false && "support for this AST node type is not yet implemented");
  }

  void static AbortUnimplemented() {
    assert(false &&
           "input files to the new fidlfmt must not contain any raw AST nodes exclusive to the old "
           "syntax");
  }
};

}  // namespace fidl::fmt

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SPAN_SEQUENCE_TREE_VISITOR_H_

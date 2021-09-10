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
  explicit SpanSequenceTreeVisitor(std::string_view file,
                                   std::vector<std::unique_ptr<Token>> tokens)
      : file_(file), tokens_(std::move(tokens)) {}
  void OnAliasDeclaration(std::unique_ptr<raw::AliasDeclaration> const& element) override;
  void OnAttributeArg(std::unique_ptr<raw::AttributeArg> const& element) override;
  void OnAttributeNew(std::unique_ptr<raw::AttributeNew> const& element) override;
  void OnAttributeListNew(std::unique_ptr<raw::AttributeListNew> const& element) override;
  void OnBinaryOperatorConstant(
      std::unique_ptr<raw::BinaryOperatorConstant> const& element) override;
  void OnCompoundIdentifier(std::unique_ptr<raw::CompoundIdentifier> const& element) override;
  void OnConstant(std::unique_ptr<raw::Constant> const& element) override;
  void OnConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const& element) override;
  void OnFile(std::unique_ptr<raw::File> const& element) override;
  void OnIdentifier(std::unique_ptr<raw::Identifier> const& element, bool ignore);
  void OnIdentifier(std::unique_ptr<raw::Identifier> const& element) override {
    OnIdentifier(element, false);
  };
  void OnIdentifierConstant(std::unique_ptr<raw::IdentifierConstant> const& element) override;
  void OnLayout(std::unique_ptr<raw::Layout> const& element) override;
  void OnInlineLayoutReference(std::unique_ptr<raw::InlineLayoutReference> const& element) override;
  void OnLayoutMember(std::unique_ptr<raw::LayoutMember> const& element) override;
  void OnLibraryDecl(std::unique_ptr<raw::LibraryDecl> const& element) override;
  void OnLiteral(std::unique_ptr<raw::Literal> const& element) override;
  void OnLiteralConstant(std::unique_ptr<raw::LiteralConstant> const& element) override;
  void OnNamedLayoutReference(std::unique_ptr<raw::NamedLayoutReference> const& element) override;
  void OnOrdinal64(raw::Ordinal64& element) override;
  void OnOrdinaledLayoutMember(std::unique_ptr<raw::OrdinaledLayoutMember> const& element) override;
  void OnParameterListNew(std::unique_ptr<raw::ParameterListNew> const& element) override;
  void OnProtocolCompose(std::unique_ptr<raw::ProtocolCompose> const& element) override;
  void OnProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> const& element) override;
  void OnProtocolMethod(std::unique_ptr<raw::ProtocolMethod> const& element) override;
  void OnResourceDeclaration(std::unique_ptr<raw::ResourceDeclaration> const& element) override;
  void OnResourceProperty(std::unique_ptr<raw::ResourceProperty> const& element) override;
  void OnServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> const& element) override;
  void OnServiceMember(std::unique_ptr<raw::ServiceMember> const& element) override;
  void OnStructLayoutMember(std::unique_ptr<raw::StructLayoutMember> const& element) override;
  void OnTypeConstructorNew(std::unique_ptr<raw::TypeConstructorNew> const& element) override;
  void OnTypeDecl(std::unique_ptr<raw::TypeDecl> const& element) override;
  void OnUsing(std::unique_ptr<raw::Using> const& element) override;
  void OnValueLayoutMember(std::unique_ptr<raw::ValueLayoutMember> const& element) override;

  // Must be called after OnFile() has been called.  Returns the result of the file fragmentation
  // work done by this class.
  MultilineSpanSequence Result();

 private:
  enum struct VisitorKind {
    kAliasDeclaration,
    kAttributeArg,
    kAttribute,
    kAttributeList,
    kBinaryOperatorFirstConstant,
    kBinaryOperatorSecondConstant,
    kCompoundIdentifier,
    kConstant,
    kConstDeclaration,
    kFile,
    kIdentifier,
    kIdentifierConstant,
    kInlineLayoutReference,
    kLayout,
    kLayoutMember,
    kLibraryDecl,
    kLiteral,
    kLiteralConstant,
    kNamedLayoutReference,
    kOrdinal64,
    kOrdinaledLayout,
    kOrdinaledLayoutMember,
    kParameterList,
    kProtocolCompose,
    kProtocolDeclaration,
    kProtocolMethod,
    kProtocolRequest,
    kProtocolResponse,
    kResourceDeclaration,
    kResourceProperty,
    kServiceDeclaration,
    kServiceMember,
    kStructLayout,
    kStructLayoutMember,
    kTypeConstructorNew,
    kTypeDecl,
    kUsing,
    kValueLayout,
    kValueLayoutMember,
  };

  // As we descend down a particular branch of the raw AST, we record the VisitorKind of each node
  // we visit in the ast_path_ member set.  Later, we can use this function to check if we are
  // "inside" of some raw AST node.  For example, we handle raw::Identifiers differently if they are
  // inside of a raw::CompoundIdentifier.  Running `IsInsideOf(VisitorKind::kCompoundIndentifier)`
  // allows us to deduce if this special handling is necessary for any raw::Identifier we visit.
  bool IsInsideOf(VisitorKind visitor_kind);

  // This function is like `IsInsideOf`, except it only checks the immediate parent node.
  bool IsDirectlyInsideOf(VisitorKind visitor_kind);

  // An RAII-ed tracking class, invoked at the start of each On*-like visitor.  It appends the
  // VisitorKind of the visitor to the ast_path_ for the life time of the On* visitor's execution,
  // allowing downstream visitors to orient themselves.  For example, OnIdentifier behaves slightly
  // differently depending on whether or not it is inside of a CompoundIdentifier.  By adding
  // VisitorKinds as we go down the tree, we're able to deduce from within OnIdentifier whether or
  // not it is contained in this node.
  class Visiting {
   public:
    Visiting(SpanSequenceTreeVisitor* ftv, VisitorKind visitor_kind);
    virtual ~Visiting();

   private:
    SpanSequenceTreeVisitor* ftv_;
  };

  // An RAII-ed base class for constructing SpanSequence's from inside On* visitor methods.  Each
  // instance of a Builder is roughly saying "make a SpanSequence out of text between the end of the
  // last processed node and the one currently being visited."
  template <typename T>
  class Builder {
    static_assert(std::is_base_of<SpanSequence, T>::value,
                  "T of Builder<T> must inherit from SpanSequence");

   public:
    Builder(SpanSequenceTreeVisitor* ftv, const Token& start, const Token& end, bool new_list);
    Builder(SpanSequenceTreeVisitor* ftv, const Token& start, bool new_list)
        : Builder(ftv, start, start, new_list){};

    // Empty builder method ensures that all Builders live until the end of their scope, enabling
    // RAII usage.  Using ` = default` as clang suggests seems to cause the compiler to throw unused
    // variable warnings when the Builder is used as part of the RAII pattern.
    ~Builder(){};

   protected:
    SpanSequenceTreeVisitor* GetFormattingTreeVisitor() { return ftv_; }
    const Token& GetStartToken() { return start_; }
    const Token& GetEndToken() { return end_; }

   private:
    SpanSequenceTreeVisitor* ftv_;
    const Token& start_;
    const Token& end_;
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
    TokenBuilder(SpanSequenceTreeVisitor* ftv, const Token& token, bool trailing_space);
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
    // Use these constructors when the entire SourceElement will be ingested by the SpanBuilder.
    SpanBuilder(SpanSequenceTreeVisitor* ftv, const raw::SourceElement& element,
                SpanSequence::Position position = SpanSequence::Position::kDefault)
        : Builder<T>(ftv, element.start_, element.end_, true), position_(position) {}
    SpanBuilder(SpanSequenceTreeVisitor* ftv, const Token& start, const Token& end,
                SpanSequence::Position position = SpanSequence::Position::kDefault)
        : Builder<T>(ftv, start, end, true), position_(position) {}

    // Use this constructor when the SourceElement will only be partially ingested by the
    // SpanBuilder.  For example, a ConstDeclaration's identifier and type_ctor members are ingested
    // into one SpanSequence, but the constant member should be in another.  Since the second
    // SpanSequence starts before the end of the SourceElement, we should use a constructor that
    // only ingests up to the start of SourceElement, but no further.
    SpanBuilder(SpanSequenceTreeVisitor* ftv, const Token& start,
                SpanSequence::Position position = SpanSequence::Position::kDefault)
        : Builder<T>(ftv, start, start, true), position_(position) {}

    ~SpanBuilder();

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
  class StatementBuilder : public Builder<T> {
   public:
    // Use this constructor when the entire SourceElement will be ingested by the StatementBuilder.
    StatementBuilder(SpanSequenceTreeVisitor* ftv, const raw::SourceElement& element,
                     SpanSequence::Position position = SpanSequence::Position::kDefault)
        : Builder<T>(ftv, element.start_, element.end_, true), position_(position) {}
    StatementBuilder(SpanSequenceTreeVisitor* ftv, const Token& start, const Token& end,
                     SpanSequence::Position position = SpanSequence::Position::kDefault)
        : Builder<T>(ftv, start, end, true), position_(position) {}

    // Use this constructor when the SourceElement will only be partially ingested by the
    // StatementBuilder.  For example, a ConstDeclaration's identifier and type_ctor members are
    // ingested into one SpanSequence, but the constant member should be in another.  Since the
    // second SpanSequence starts before the end of the SourceElement, we should use a constructor
    // that only ingests up to the start of SourceElement, but no further.
    StatementBuilder(SpanSequenceTreeVisitor* ftv, const Token& start,
                     SpanSequence::Position position = SpanSequence::Position::kDefault)
        : Builder<T>(ftv, start, start, true), position_(position) {}

    ~StatementBuilder();

   private:
    const SpanSequence::Position position_;
  };

 public:
  // Given an optional Token from our source file, ingest up to but NOT including that Token.  The
  // token passed in must be greater than or equal to the token identified by the next_token_index_
  // member variable.  If the first argument is nullopt, this function will ingest to the end of the
  // token list.
  std::optional<std::unique_ptr<SpanSequence>> IngestUpTo(
      std::optional<Token> until,
      SpanSequence::Position position = SpanSequence::Position::kDefault);

  // Given an optional Token from our source file, ingest up to and including that Token.  The token
  // passed in must be greater than or equal to the token identified by the next_token_index_ member
  // variable.  If the first argument is nullopt, this function will ingest to the end of the
  // token list.
  std::optional<std::unique_ptr<SpanSequence>> IngestUpToAndIncluding(
      std::optional<Token> until,
      SpanSequence::Position position = SpanSequence::Position::kDefault);

  // Given an optional token kind, ingest up to and including the first instance of that token kind,
  // taking care to include any inline comments that may be trailing after that instance.  In other
  // words, if we call this method on a string_view that looks like `foo;\n` or `foo; bar`, we
  // should expect to ingest the `foo;` portion.  But if we call it on `foo; // bar\n`, we should
  // expect to ingest the entire thing, trailing comment included. If the first argument is nullopt,
  // this function will ingest to the end of the token list.
  std::optional<std::unique_ptr<SpanSequence>> IngestUpToAndIncludingTokenKind(
      std::optional<Token::Kind> until_kind,
      SpanSequence::Position position = SpanSequence::Position::kDefault);

  // Ingest all remaining tokens until the end of the file.
  std::optional<std::unique_ptr<SpanSequence>> IngestRestOfFile();

  // Sugar for IngestUpToAndIncludingTokenKind(Token::kSemicolon)`.
  std::optional<std::unique_ptr<SpanSequence>> IngestUpToAndIncludingSemicolon();

  // Stores that path in the raw AST of the node currently being visited.  See the comment on the
  // `Visiting` class for more on why this is useful.
  std::vector<VisitorKind> ast_path_;

  // We need to invoke the OnAttributesList visitor manually, to ensure that it attributes are
  // handled independently of the declaration they are attached to.  This means that every
  // AttributeList will be visited twice: once during this manual invocation, and then again during
  // the regular course of the TreeVisitor for the raw AST node the AttributeList is attached to.
  // To ensure that the AttributeList is not processed twice, each new OnAttributeList invocation
  // checks against this set to ensure that the AttributeList in question has not already been
  // visited.

  // We need to invoke certain On* visitors, like OnAttributeList or OnIdentifier, manually prior to
  // delegating to the original TreeVisitor logic for their parent node, which will visit them
  // again.  This is necessary when we want to handle child AST nodes in a different order than that
  // which they are visited in by the default TreeVisitor of that kind.  For example, when in
  // OnProtocolDeclaration, we need to visit the attached attributes before visiting the first token
  // of the declaration (in this case, "protocol") itself.  If we did not do this, and instead
  // delegated the task to the TreeVisitor, the resulting output wold be:
  //
  //   protocol @foo {...
  //
  // To avoid this "double visit" problem, we maintain a set of pointers to SourceElements we've
  // already visited.
  std::set<raw::SourceElement*> already_seen_;

  // A stack that keeps track of the CompositeSpanSequence we are currently building.  It is a list
  // of that CompositeSpanSequence's children.  When the child list has been filled out, it is
  // popped off the stack and pushed onto the new top element as its child.
  //
  // When this class is constructed, one element is added to this stack, serving as the "root"
  // SpanSequence for the file.  Calling this class' Result() method pops that element off and
  // returns it, representing the fully processed SpanSequence tree for the given source file, and
  // exhausting this class.
  std::stack<std::vector<std::unique_ptr<SpanSequence>>> building_;

  // A view into the entire source file being formatted.
  const std::string_view file_;

  // An ordered list of all tokens (including comments) in the source file.
  std::vector<std::unique_ptr<Token>> tokens_;

  // The index of the next token to be visited.
  size_t next_token_index_ = 0;

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

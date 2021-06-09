// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/span_sequence_tree_visitor.h"

#include "fidl/raw_ast.h"
#include "fidl/span_sequence.h"
#include "fidl/tree_visitor.h"

namespace fidl::fmt {

namespace {

enum struct CommentStyle {
  kInline,
  kStandalone,
};

// An entire line (if comment_style == kStandalone) or at least the trailing portion of it (if
// comment_style == kInline) is a comment.  This function ingests up to the end of that line.  The
// text passed to this function must include and start with the `//` character pair that triggered
// this function call (ie, comment lines are ingested with their leading double slashes).
size_t IngestCommentLine(std::string_view text, size_t leading_blank_lines,
                         CommentStyle comment_style, AtomicSpanSequence* out) {
  size_t chars_seen = 0;
  for (char const& c : text) {
    chars_seen++;

    // Ignore any character besides a newline, as its part of the comment text.
    if (c != '\n') {
      continue;
    }

    // Ok, we've reached the end of the comment line.  Figure out where if fits into the bigger
    // picture: its either an inline comment, the first line of a standalone comment, or a
    // continuing line of a standalone comment.
    auto line = std::string_view(text.data(), chars_seen - 1);
    if (comment_style == CommentStyle::kInline) {
      // The first part of this line was source code, so the last SpanSequence must be an
      // AtomicSpanSequence.  Add the inline comment to that node, then close it.
      std::unique_ptr<InlineCommentSpanSequence> inline_comment =
          std::make_unique<InlineCommentSpanSequence>(line);
      inline_comment->Close();
      out->AddChild(std::move(inline_comment));
      return chars_seen;
    }

    auto last_child = out->GetLastChild();
    if (last_child != nullptr && last_child->GetKind() == SpanSequence::Kind::kStandaloneComment) {
      // There was only a comment on this line, but it is part of a larger, still open comment
      // block.
      auto open_standalone_comment = static_cast<StandaloneCommentSpanSequence*>(last_child);
      open_standalone_comment->AddLine(line, leading_blank_lines);
      return chars_seen;
    }

    // This line commences a new standalone comment block of one or more lines. That means that the
    // currently open SpanSequence, if one exists, needs to be closed.
    auto standalone_comment = std::make_unique<StandaloneCommentSpanSequence>(leading_blank_lines);
    standalone_comment->AddLine(line);
    out->AddChild(std::move(standalone_comment));
    return chars_seen;
  }

  return chars_seen;
}

// Converts some contiguous non-whitespace span of source text into a TokenSpanSequence.  All source
// code (ie, all text not preceded on its line by `//`) will pass through this function to be broken
// into tokens.
void AppendToken(std::string_view word, size_t leading_blank_lines, AtomicSpanSequence* out) {
  if (!word.empty() && word[0] != '\0') {
    // TODO(fxbug.dev/73507): add more variants to the regex as we run across them when adding
    //  support for more raw AST node types.
    static std::regex needs_trailing_space_regex("[a-z_]+|=|\\|");
    auto token_span_sequence = std::make_unique<TokenSpanSequence>(word, leading_blank_lines);
    if (std::regex_match(std::string(word), needs_trailing_space_regex)) {
      token_span_sequence->SetTrailingSpace(true);
    }

    token_span_sequence->Close();
    out->AddChild(std::move(token_span_sequence));
  }
}

// For each line we ingest, we want to know there things: how many characters were on that line,
// whether we encountered a semicolon, and whether or not it was a whitespace-only line.  That last
// boolean helps us decide how many leading newlines to put before the SpanSequence being
// constructed.
struct IngestLineResult {
  size_t chars_seen;
  bool semi_colon_seen = false;
  bool is_all_whitespace = false;
};

// Ingests source file text until the end of a line or the end of the file, whichever comes first,
// with one exception: ingestion stops immediately if a semicolon followed by a non-comment span is
// encountered, and only the portion up to the semicolon is ingested.
IngestLineResult IngestLine(std::string_view text, size_t leading_newlines,
                            AtomicSpanSequence* out) {
  const size_t leading_blank_lines = leading_newlines == 0 ? 0 : leading_newlines - 1;
  size_t chars_seen = 0;
  bool prev_is_slash = false;
  bool source_seen = false;
  bool semi_colon_seen = false;
  std::optional<const char*> source;
  for (char const& c : text) {
    chars_seen++;

    if (prev_is_slash && c != '/') {
      prev_is_slash = false;
      if (!source) {
        source = &c - 1;
      }
    }

    switch (c) {
      case '\n': {
        // The line has ended.  Stuff whatever we have so far into a TokenSpanSequence and return.
        if (source.has_value()) {
          AppendToken(std::string_view(source.value(), &c - source.value()),
                      source_seen ? 0 : leading_blank_lines, out);
          source = std::nullopt;
          source_seen = true;
        }
        return IngestLineResult{chars_seen, semi_colon_seen, !source_seen};
      }
      case ' ':
      case '\t': {
        // A token not in the raw AST such as "resource" or "using" has ended. Append it to the
        // working set, and start the next atomic.
        if (source.has_value()) {
          AppendToken(std::string_view(source.value(), &c - source.value()),
                      source_seen ? 0 : leading_blank_lines, out);
          source = std::nullopt;
          source_seen = true;
        }
        break;
      }
      case ';': {
        // Always close out the currently building TokenSpanSequence when we encounter a semi-colon.
        //  If no such sequence is being built, create a new one for just the lone semi-colon.
        semi_colon_seen = true;
        if (source.has_value()) {
          AppendToken(std::string_view(source.value(), (&c + 1) - source.value()),
                      source_seen ? 0 : leading_blank_lines, out);
          source = std::nullopt;
          source_seen = true;
        } else {
          AppendToken(std::string_view(&c, 1), source_seen ? 0 : leading_blank_lines, out);
          source = std::nullopt;
          source_seen = true;
        }
        break;
      }
      case '/': {
        // This MAY be the start of a comment...
        if (prev_is_slash) {
          // ... it is!
          chars_seen -= 2;
          auto comment_start = &c - 1;
          if (source.has_value()) {
            AppendToken(std::string_view(source.value(), comment_start - source.value()),
                        source_seen ? 0 : leading_blank_lines, out);
            source = std::nullopt;
            source_seen = true;
          }

          auto comment_text = std::string_view(comment_start, text.size() - chars_seen);
          if (source_seen || leading_newlines == 0) {
            chars_seen +=
                IngestCommentLine(comment_text, leading_blank_lines, CommentStyle::kInline, out);
            return IngestLineResult{chars_seen, semi_colon_seen};
          }

          chars_seen +=
              IngestCommentLine(comment_text, leading_blank_lines, CommentStyle::kStandalone, out);
          return IngestLineResult{chars_seen, semi_colon_seen};
        }

        // We'll need the next char to be a slash to officially start the comment.
        prev_is_slash = true;
        break;
      }
      default: {
        if (semi_colon_seen) {
          return IngestLineResult{chars_seen - 1, semi_colon_seen};
        }
        if (!source) {
          source = &c;
        }
        break;
      }
    }
  }

  if (source.has_value()) {
    AppendToken(std::string_view(source.value(), (text.data() + text.size()) - source.value()),
                source_seen ? 0 : leading_blank_lines, out);
    source = std::nullopt;
    source_seen = true;
  }
  return IngestLineResult{chars_seen, semi_colon_seen, !source_seen};
}

// Is the last leaf of the SpanSequence tree with its root at the provided SpanSequence a
// CommentSpanSequence?
bool EndsWithComment(const std::unique_ptr<SpanSequence>& span_sequence) {
  if (span_sequence->IsComposite()) {
    auto as_composite = static_cast<CompositeSpanSequence*>(span_sequence.get());
    if (as_composite->IsEmpty())
      return false;

    const auto& children = as_composite->GetChildren();
    return EndsWithComment(children.back());
  }

  return span_sequence->IsComment();
}

// Adds in spaces between all of the non-comment children of a list of SpanSequences.  This means
// that a trailing space is added to every non-comment child SpanSequence, except the last one.
void AddSpacesBetweenChildren(const std::vector<std::unique_ptr<SpanSequence>>& list) {
  std::optional<size_t> last_non_comment_index;
  const auto last_non_comment_it =
      std::find_if(list.crbegin(), list.crend(),
                   [&](const std::unique_ptr<SpanSequence>& s) { return !s->IsComment(); });
  if (last_non_comment_it != list.rend()) {
    last_non_comment_index = std::distance(last_non_comment_it, list.rend()) - 1;
  }

  for (size_t i = 0; i < list.size(); i++) {
    const auto& child = list[i];
    if (!EndsWithComment(child) && i < last_non_comment_index.value_or(0)) {
      child->SetTrailingSpace(true);
    }
  }
}

// Used to ensure that there are no leading blank lines for the SpanSequence tree with its root at
// the provided SpanSequence.  This means recursing down the leftmost branch of the tree, setting
// each "leading_new_lines_" value to 0 as we go.
void ClearLeadingBlankLines(std::unique_ptr<SpanSequence>& span_sequence) {
  if (span_sequence->IsComposite()) {
    // If the first item in the list is a CompositeSpanSequence, its first child's
    // leading_blank_lines_ value will be "hoisted" up to the parent when it's closed.  To ensure
    // that the CompositeSpanSequence retains a zero in this position when that happens, we must
    // set that leading_blank_line_ value to 0 as well.  We need to repeat this process recursively.
    auto as_composite = static_cast<CompositeSpanSequence*>(span_sequence.get());
    if (!as_composite->IsEmpty() && !as_composite->GetChildren()[0]->IsComment()) {
      ClearLeadingBlankLines(as_composite->EditChildren()[0]);
    }
  }

  span_sequence->SetLeadingBlankLines(0);
}

// Consider the following FIDL:
//
//   @foo
//
//   type Foo = ...;
//
// We want to ensure that attribute-carrying declarations like the one above never have a blank line
// between the attribute block and the declaration itself.  To accomplish this goal this function
// checks to see if an attribute block exists for the raw AST node currently being processed.  If it
// does, the first element in the currently open SpanSequence list has its leading_blank_lines
// overwritten to 0.
void ClearBlankLinesAfterAttributeList(const std::unique_ptr<raw::AttributeListNew>& attrs,
                                       std::vector<std::unique_ptr<SpanSequence>>& list) {
  if (attrs != nullptr && !list.empty()) {
    ClearLeadingBlankLines(list[0]);
  }
}

}  // namespace

std::optional<std::unique_ptr<SpanSequence>> SpanSequenceTreeVisitor::IngestUntil(
    const char* limit, bool stop_at_semicolon, SpanSequence::Position position) {
  const auto ingesting = std::string_view(uningested_.data(), (limit - uningested_.data()) + 1);
  auto atomic = std::make_unique<AtomicSpanSequence>(position);
  size_t chars_seen = 0;

  while (ingesting.data() + chars_seen <= limit) {
    // If this is the very first character in the file, set "preceding_newlines" to 1, so that the
    // first comment (which is likely, since most files start with a copyright notice) is not
    // taken to be an inline comment.
    auto preceding_newlines =
        uningested_.data() == file_.data() && chars_seen == 0 ? 1 : preceding_newlines_;
    auto result = IngestLine(ingesting.substr(chars_seen), preceding_newlines, atomic.get());

    chars_seen += result.chars_seen;
    bool reached_newline = result.chars_seen && *(uningested_.data() + (chars_seen - 1)) == '\n';
    if (stop_at_semicolon && result.semi_colon_seen) {
      // If we saw a semicolon, this line could not have been empty, so reset the counter.
      preceding_newlines_ = reached_newline ? 1 : 0;
      break;
    }

    if (result.is_all_whitespace) {
      // If the line was just blank space, increment the counter.
      preceding_newlines_ += reached_newline ? 1 : 0;
    } else {
      // If the line was not empty, make sure to reset the counter.
      preceding_newlines_ = reached_newline ? 1 : 0;
    }
  }
  uningested_ = uningested_.substr(std::min(chars_seen, uningested_.size()));

  if (!atomic->IsEmpty()) {
    atomic->Close();
    return std::move(atomic);
  }
  return std::nullopt;
}

std::optional<std::unique_ptr<SpanSequence>> SpanSequenceTreeVisitor::IngestUntilEndOfFile() {
  return IngestUntil(file_.data() + (file_.size() - 1));
}

std::optional<std::unique_ptr<SpanSequence>> SpanSequenceTreeVisitor::IngestUntilSemicolon() {
  return IngestUntil(file_.data() + (file_.size() - 1), true);
}

bool SpanSequenceTreeVisitor::IsInsideOf(VisitorKind visitor_kind) {
  return std::find(ast_path_.begin(), ast_path_.end(), visitor_kind) != ast_path_.end();
}

SpanSequenceTreeVisitor::Visiting::Visiting(SpanSequenceTreeVisitor* ftv, VisitorKind visitor_kind)
    : ftv_(ftv) {
  this->ftv_->ast_path_.push_back(visitor_kind);
}

SpanSequenceTreeVisitor::Visiting::~Visiting() { this->ftv_->ast_path_.pop_back(); }

template <typename T>
SpanSequenceTreeVisitor::Builder<T>::Builder(SpanSequenceTreeVisitor* ftv, const Token& start,
                                             const Token& end, bool new_list)
    : ftv_(ftv), start_(start), end_(end) {
  if (new_list)
    this->GetFormattingTreeVisitor()->building_.push(std::vector<std::unique_ptr<SpanSequence>>());

  auto prelude = ftv_->IngestUntil(start_.data().data() - 1);
  if (prelude.has_value())
    ftv_->building_.top().push_back(std::move(prelude.value()));
}

template <typename T>
SpanSequenceTreeVisitor::Builder<T>::~Builder<T>() {
  const auto tracking = end_.data().data() + end_.data().size();
  if (tracking >= ftv_->uningested_.data()) {
    ftv_->uningested_ = tracking;
  }
}

SpanSequenceTreeVisitor::TokenBuilder::TokenBuilder(SpanSequenceTreeVisitor* ftv,
                                                    const raw::SourceElement& element,
                                                    bool has_trailing_space)
    : Builder<TokenSpanSequence>(ftv, element.start_, element.end_, false) {
  const auto leading_newlines = this->GetFormattingTreeVisitor()->preceding_newlines_;
  const size_t leading_blank_lines = leading_newlines == 0 ? 0 : leading_newlines - 1;

  auto token_span_sequence =
      std::make_unique<TokenSpanSequence>(element.span().data(), leading_blank_lines);
  token_span_sequence->SetTrailingSpace(has_trailing_space);
  token_span_sequence->Close();
  this->GetFormattingTreeVisitor()->building_.top().push_back(std::move(token_span_sequence));

  ftv->preceding_newlines_ = 0;
}

template <typename T>
SpanSequenceTreeVisitor::SpanBuilder<T>::~SpanBuilder<T>() {
  // Ingest any remaining text between the last processed child and the end token of the span.  This
  // text may not retain any leading blank lines or trailing spaces.
  auto end_view = this->GetEndToken().data();
  auto postscript = this->GetFormattingTreeVisitor()->IngestUntil(
      end_view.data() + (end_view.size() - 1), false, SpanSequence::Position::kNewlineUnindented);
  if (postscript.has_value()) {
    const auto& top = this->GetFormattingTreeVisitor()->building_.top();
    if (!top.empty() && !EndsWithComment(top.back())) {
      postscript.value()->SetLeadingBlankLines(0);
    }

    postscript.value()->SetTrailingSpace(false);
    this->GetFormattingTreeVisitor()->building_.top().push_back(std::move(postscript.value()));
  }

  auto parts = std::move(this->GetFormattingTreeVisitor()->building_.top());
  auto composite_span_sequence = std::make_unique<T>(std::move(parts), this->position_);
  composite_span_sequence->CloseChildren();

  this->GetFormattingTreeVisitor()->building_.pop();
  this->GetFormattingTreeVisitor()->building_.top().push_back(std::move(composite_span_sequence));
}

template <typename T>
SpanSequenceTreeVisitor::StatementBuilder<T>::~StatementBuilder<T>() {
  auto parts = std::move(this->GetFormattingTreeVisitor()->building_.top());
  auto composite_span_sequence = std::make_unique<T>(std::move(parts), this->position_);
  auto semicolon_span_sequence = this->GetFormattingTreeVisitor()->IngestUntilSemicolon().value();

  // Append the semicolon_span_sequence to the last child in the composite_span_sequence, if it
  // exists.
  auto last_child = composite_span_sequence->GetLastChild();
  if (last_child != nullptr && !last_child->IsClosed()) {
    assert(last_child->IsComposite() && "cannot append semicolon to non-composite SpanSequence");
    auto last_child_as_composite = static_cast<CompositeSpanSequence*>(last_child);
    last_child_as_composite->AddChild(std::move(semicolon_span_sequence));
  }
  composite_span_sequence->CloseChildren();

  this->GetFormattingTreeVisitor()->building_.pop();
  this->GetFormattingTreeVisitor()->building_.top().push_back(std::move(composite_span_sequence));
}

void SpanSequenceTreeVisitor::OnAliasDeclaration(
    const std::unique_ptr<raw::AliasDeclaration>& element) {
  const auto visiting = Visiting(this, VisitorKind::kAliasDeclaration);
  const auto& attrs = std::get<std::unique_ptr<raw::AttributeListNew>>(element->attributes);
  if (attrs != nullptr) {
    OnAttributeListNew(attrs);
  }

  const auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnAliasDeclaration(element);
  AddSpacesBetweenChildren(building_.top());
  ClearBlankLinesAfterAttributeList(attrs, building_.top());
}

void SpanSequenceTreeVisitor::OnAttributeArg(const raw::AttributeArg& element) {
  const auto visiting = Visiting(this, VisitorKind::kAttributeArg);
  const auto builder = SpanBuilder<AtomicSpanSequence>(this, element);

  // Since the name member of the Attribute is not passed in as a raw AST node, but rather as a
  // string, it will be processed as the prelude to the value member's TokenSpanSequence.
  TreeVisitor::OnLiteral(element.value);
}

void SpanSequenceTreeVisitor::OnAttributeNew(const raw::AttributeNew& element) {
  const auto visiting = Visiting(this, VisitorKind::kAttribute);

  // Special case: this attribute is actually a doc comment.  Treat it like any other comment type,
  // and ingest until the last newline in the doc comment.
  if (element.provenance == raw::AttributeNew::Provenance::kDocComment) {
    const char* last_char = element.end_.data().data() + element.end_.data().size();
    auto doc_comment = IngestUntil(last_char);
    if (doc_comment.has_value())
      building_.top().push_back(std::move(doc_comment.value()));
    return;
  }

  // Special case: attribute with no arguments.  Just make a TokenSpanSequence out of the @ string
  // and exit.
  if (element.args.empty()) {
    const auto builder =
        SpanBuilder<AtomicSpanSequence>(this, element, SpanSequence::Position::kNewlineUnindented);
    const auto token_builder = TokenBuilder(this, element, false);
    return;
  }

  // This attribute has at least one argument.  For each argument, first ingest the prelude (usually
  // the preceding comment), but at it as a suffix to the previous attribute instead of as a prefix
  // to the current one.  If we did not do this, we'd end up with formatting like:
  //
  //   @foo
  //           ("my very very ... very long arg 1"
  //           , "my very very ... very long arg 2")
  const auto builder = SpanBuilder<DivisibleSpanSequence>(
      this, element.args[0].start_, SpanSequence::Position::kNewlineUnindented);
  std::optional<SpanBuilder<AtomicSpanSequence>> arg_builder;
  for (const auto& arg : element.args) {
    auto postscript = IngestUntil(arg.start_.data().data() - 1);
    if (postscript.has_value())
      building_.top().push_back(std::move(postscript.value()));

    arg_builder.emplace(this, arg);
    TreeVisitor::OnAttributeArg(arg);
  }

  // Make sure to delete the last argument, so that its destructor is called and it is properly
  // added to the "building_" stack.
  arg_builder.reset();

  // Ingest the closing ")" character, and append it to the final argument.
  auto postscript = IngestUntil(element.end_.data().data() + (element.end_.data().size() - 1));
  if (postscript.has_value()) {
    auto last_argument_span_sequence =
        static_cast<AtomicSpanSequence*>(building_.top().back().get());
    last_argument_span_sequence->AddChild(std::move(postscript.value()));
  }

  // At this point, we should have a set of atomic span sequences with children like:
  //
  //   «@foo(»«"arg1",»«"arg2"»,«"..."»,«"argN")»
  //
  // We want to make sure there is a space between each of these child elements, except for the
  // first to, to produce an output like:
  //
  //   @foo("arg1", "arg2", "...", "argN")
  //
  // To accomplish this, we simply add the trailing spaces to every non-comment element except the
  // last, then remove the trailing space from the first element.
  AddSpacesBetweenChildren(building_.top());
  building_.top()[0]->SetTrailingSpace(false);
}

void SpanSequenceTreeVisitor::OnAttributeListNew(
    const std::unique_ptr<raw::AttributeListNew>& element) {
  if (attribute_lists_seen_.insert(element.get()).second) {
    const auto visiting = Visiting(this, VisitorKind::kAttributeList);
    const auto indent = IsInsideOf(VisitorKind::kLayoutMember)
                            ? SpanSequence::Position::kNewlineIndented
                            : SpanSequence::Position::kNewlineUnindented;
    const auto builder = SpanBuilder<MultilineSpanSequence>(this, *element, indent);
    TreeVisitor::OnAttributeListNew(element);

    // Remove all blank lines between attributes.
    auto& attr_span_sequences = building_.top();
    for (size_t i = 1; i < attr_span_sequences.size(); ++i) {
      auto& child_span_sequence = attr_span_sequences[i];
      if (!child_span_sequence->IsComment()) {
        ClearLeadingBlankLines(child_span_sequence);
      }
    }
  }
}

void SpanSequenceTreeVisitor::OnBinaryOperatorConstant(
    const std::unique_ptr<raw::BinaryOperatorConstant>& element) {
  // We need a separate scope, so that each operand receives a different visitor kind.  This is
  // important because OnLiteral visitor behaves different for the last constant in the chain: it
  // requires trailing spaces on all constants except the last.
  {
    const auto visiting = Visiting(this, VisitorKind::kBinaryOperatorFirstConstant);
    const auto operand_builder = SpanBuilder<AtomicSpanSequence>(this, *element->left_operand);
    TreeVisitor::OnConstant(element->left_operand);
  }

  const auto visiting = Visiting(this, VisitorKind::kBinaryOperatorSecondConstant);
  const auto operand_builder = SpanBuilder<AtomicSpanSequence>(this, *element->right_operand);
  TreeVisitor::OnConstant(element->right_operand);
}

void SpanSequenceTreeVisitor::OnCompoundIdentifier(
    const std::unique_ptr<raw::CompoundIdentifier>& element) {
  const auto visiting = Visiting(this, VisitorKind::kCompoundIdentifier);
  const auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnCompoundIdentifier(element);
}

void SpanSequenceTreeVisitor::OnConstant(const std::unique_ptr<raw::Constant>& element) {
  const auto visiting = Visiting(this, VisitorKind::kConstant);
  const auto span_builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnConstant(element);
}

void SpanSequenceTreeVisitor::OnConstDeclaration(
    const std::unique_ptr<raw::ConstDeclaration>& element) {
  const auto visiting = Visiting(this, VisitorKind::kConstDeclaration);
  const auto& attrs = std::get<std::unique_ptr<raw::AttributeListNew>>(element->attributes);
  if (attrs != nullptr) {
    OnAttributeListNew(attrs);
  }

  const auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);

  // We need a separate scope for these two nodes, as they are meant to be their own
  // DivisibleSpanSequence, but no raw AST node or visitor exists for grouping them.
  {
    const auto lhs_builder = SpanBuilder<DivisibleSpanSequence>(this, element->start_);

    // Keep the "const" keyword atomic with the name of the declaration.
    {
      const auto name_builder = SpanBuilder<AtomicSpanSequence>(this, *element->identifier);
      OnIdentifier(element->identifier);
    }

    // Similarly, keep the type constructor atomic as well.
    {
      const auto& type_ctor_new =
          std::get<std::unique_ptr<raw::TypeConstructorNew>>(element->type_ctor);
      const auto type_ctor_new_builder = SpanBuilder<AtomicSpanSequence>(this, *type_ctor_new);
      OnTypeConstructor(element->type_ctor);
    }
    AddSpacesBetweenChildren(building_.top());
  }

  OnConstant(element->constant);
  AddSpacesBetweenChildren(building_.top());
  ClearBlankLinesAfterAttributeList(attrs, building_.top());
}

void SpanSequenceTreeVisitor::OnFile(const std::unique_ptr<raw::File>& element) {
  const auto visiting = Visiting(this, VisitorKind::kFile);
  building_.push(std::vector<std::unique_ptr<SpanSequence>>());

  DeclarationOrderTreeVisitor::OnFile(element);

  if (!uningested_.empty()) {
    auto footer = IngestUntilEndOfFile();
    if (footer.has_value())
      building_.top().push_back(std::move(footer.value()));
  }
}

void SpanSequenceTreeVisitor::OnIdentifier(const std::unique_ptr<raw::Identifier>& element) {
  const auto visiting = Visiting(this, VisitorKind::kIdentifier);
  if (IsInsideOf(VisitorKind::kCompoundIdentifier)) {
    const auto builder = TokenBuilder(this, *element, false);
    TreeVisitor::OnIdentifier(element);
  } else {
    const auto span_builder = SpanBuilder<AtomicSpanSequence>(this, *element);
    const auto token_builder = TokenBuilder(this, *element, false);
    TreeVisitor::OnIdentifier(element);
  }
}

void SpanSequenceTreeVisitor::OnLiteral(const std::unique_ptr<raw::Literal>& element) {
  const auto visiting = Visiting(this, VisitorKind::kLiteral);
  const auto trailing_space = IsInsideOf(VisitorKind::kBinaryOperatorFirstConstant);
  const auto builder = TokenBuilder(this, *element, trailing_space);
  TreeVisitor::OnLiteral(element);
}

void SpanSequenceTreeVisitor::OnIdentifierConstant(
    const std::unique_ptr<raw::IdentifierConstant>& element) {
  const auto visiting = Visiting(this, VisitorKind::kIdentifierConstant);
  TreeVisitor::OnIdentifierConstant(element);
}

void SpanSequenceTreeVisitor::OnLayout(const std::unique_ptr<raw::Layout>& element) {
  const auto visiting = Visiting(this, VisitorKind::kLayout);

  // Special case: an empty layout (ex: `struct {}`) should always be atomic.
  if (element->members.empty()) {
    const auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
    return;
  }

  const auto builder =
      SpanBuilder<MultilineSpanSequence>(this, element->members[0]->start_, element->end_);
  TreeVisitor::OnLayout(element);
}

void SpanSequenceTreeVisitor::OnLayoutMember(const std::unique_ptr<raw::LayoutMember>& element) {
  const auto visiting = Visiting(this, VisitorKind::kLayoutMember);
  TreeVisitor::OnLayoutMember(element);
}

void SpanSequenceTreeVisitor::OnLibraryDecl(const std::unique_ptr<raw::LibraryDecl>& element) {
  const auto visiting = Visiting(this, VisitorKind::kLibraryDecl);
  const auto& attrs = std::get<std::unique_ptr<raw::AttributeListNew>>(element->attributes);
  if (attrs != nullptr) {
    OnAttributeListNew(attrs);
  }

  const auto builder = StatementBuilder<AtomicSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnLibraryDecl(element);
  ClearBlankLinesAfterAttributeList(attrs, building_.top());
}

void SpanSequenceTreeVisitor::OnLiteralConstant(
    const std::unique_ptr<raw::LiteralConstant>& element) {
  const auto visiting = Visiting(this, VisitorKind::kLiteralConstant);
  TreeVisitor::OnLiteralConstant(element);
}

void SpanSequenceTreeVisitor::OnNamedLayoutReference(
    const std::unique_ptr<raw::NamedLayoutReference>& element) {
  const auto visiting = Visiting(this, VisitorKind::kNamedLayoutReference);
  const auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnNamedLayoutReference(element);
}

void SpanSequenceTreeVisitor::OnStructLayoutMember(
    const std::unique_ptr<raw::StructLayoutMember>& element) {
  const auto visiting = Visiting(this, VisitorKind::kStructLayoutMember);
  if (element->attributes != nullptr) {
    OnAttributeListNew(element->attributes);
  }

  const auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineIndented);
  TreeVisitor::OnStructLayoutMember(element);
  AddSpacesBetweenChildren(building_.top());
  ClearBlankLinesAfterAttributeList(element->attributes, building_.top());
}

void SpanSequenceTreeVisitor::OnTypeConstructorNew(
    const std::unique_ptr<raw::TypeConstructorNew>& element) {
  const auto visiting = Visiting(this, VisitorKind::kTypeConstructorNew);

  if (element->layout_ref->kind == raw::LayoutReference::Kind::kInline) {
    TreeVisitor::OnTypeConstructorNew(element);
  } else {
    const auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
    TreeVisitor::OnTypeConstructorNew(element);
  }
}

void SpanSequenceTreeVisitor::OnTypeDecl(const std::unique_ptr<raw::TypeDecl>& element) {
  const auto visiting = Visiting(this, VisitorKind::kTypeDecl);
  if (element->attributes != nullptr) {
    OnAttributeListNew(element->attributes);
  }

  const auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnTypeDecl(element);
  AddSpacesBetweenChildren(building_.top());
  ClearBlankLinesAfterAttributeList(element->attributes, building_.top());
}

void SpanSequenceTreeVisitor::OnUsing(const std::unique_ptr<raw::Using>& element) {
  const auto visiting = Visiting(this, VisitorKind::kUsing);
  const auto& attrs = std::get<std::unique_ptr<raw::AttributeListNew>>(element->attributes);
  if (attrs != nullptr) {
    OnAttributeListNew(attrs);
  }

  const auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnUsing(element);
  AddSpacesBetweenChildren(building_.top());
  ClearBlankLinesAfterAttributeList(attrs, building_.top());
}

MultilineSpanSequence SpanSequenceTreeVisitor::Result() {
  if (building_.empty()) {
    assert(false && "Result() must be called exactly once after OnFile()");
  }
  auto result = MultilineSpanSequence(std::move(building_.top()));
  result.Close();
  building_.pop();
  return result;
}

}  // namespace fidl::fmt

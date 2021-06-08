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
    static std::regex needs_trailing_space_regex("[a-z_]+|=|:|\\|");
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
IngestLineResult IngestLine(std::string_view text, bool is_partial_line, size_t leading_blank_lines,
                            AtomicSpanSequence* out) {
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
          if (source_seen || is_partial_line) {
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
    if (!child->IsComment() && i < last_non_comment_index.value_or(0)) {
      child->SetTrailingSpace(true);
    }
  }
}

}  // namespace

std::optional<std::unique_ptr<SpanSequence>> SpanSequenceTreeVisitor::IngestUntil(
    const char* limit, bool stop_at_semicolon) {
  const size_t leading_blank_lines = empty_lines_;
  bool is_partial_line = !leading_blank_lines && uningested_.data() > file_.data() &&
                         *(uningested_.data() - 1) != '\n';
  const auto ingesting = std::string_view(uningested_.data(), (limit - uningested_.data()) + 1);
  size_t chars_seen = 0;
  size_t empty_lines = 0;

  auto atomic =
      std::make_unique<AtomicSpanSequence>(SpanSequence::Position::kDefault, leading_blank_lines);
  while (ingesting.data() + chars_seen <= limit) {
    auto result =
        IngestLine(ingesting.substr(chars_seen), is_partial_line, empty_lines, atomic.get());
    chars_seen += result.chars_seen;
    if (stop_at_semicolon && result.semi_colon_seen) {
      break;
    }
    if (is_partial_line) {
      is_partial_line = false;
    } else {
      empty_lines = result.is_all_whitespace ? empty_lines + 1 : 0;
    }
  }
  uningested_ = uningested_.substr(std::min(chars_seen, uningested_.size()));
  empty_lines_ = empty_lines;

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
  auto tracking = end_.data().data() + end_.data().size();
  if (tracking >= ftv_->uningested_.data()) {
    ftv_->uningested_ = tracking;
  }
}

SpanSequenceTreeVisitor::TokenBuilder::TokenBuilder(SpanSequenceTreeVisitor* ftv,
                                                    const raw::SourceElement& element,
                                                    bool has_trailing_space)
    : Builder<TokenSpanSequence>(ftv, element.start_, element.end_, false) {
  auto token_span_sequence = std::make_unique<TokenSpanSequence>(
      element.span().data(), this->GetFormattingTreeVisitor()->empty_lines_);
  token_span_sequence->SetTrailingSpace(has_trailing_space);
  token_span_sequence->Close();
  this->GetFormattingTreeVisitor()->building_.top().push_back(std::move(token_span_sequence));
}

template <typename T>
SpanSequenceTreeVisitor::SpanBuilder<T>::~SpanBuilder<T>() {
  auto parts = std::move(this->GetFormattingTreeVisitor()->building_.top());
  auto composite_span_sequence = std::make_unique<T>(
      std::move(parts), this->position_, this->GetFormattingTreeVisitor()->empty_lines_);
  composite_span_sequence->CloseChildren();

  this->GetFormattingTreeVisitor()->building_.pop();
  this->GetFormattingTreeVisitor()->building_.top().push_back(std::move(composite_span_sequence));
}

template <typename T>
SpanSequenceTreeVisitor::StatementBuilder<T>::~StatementBuilder<T>() {
  auto parts = std::move(this->GetFormattingTreeVisitor()->building_.top());
  auto semicolon_span_sequence = this->GetFormattingTreeVisitor()->IngestUntilSemicolon().value();
  auto composite_span_sequence = std::make_unique<T>(
      std::move(parts), this->position_, this->GetFormattingTreeVisitor()->empty_lines_);

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
  auto visiting = Visiting(this, VisitorKind::kAliasDeclaration);
  auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnAliasDeclaration(element);
  AddSpacesBetweenChildren(building_.top());
}

void SpanSequenceTreeVisitor::OnBinaryOperatorConstant(
    const std::unique_ptr<raw::BinaryOperatorConstant>& element) {
  // We need a separate scope, so that each operand receives a different visitor kind.  This is
  // important because OnLiteral visitor behaves different for the last constant in the chain: it
  // requires trailing spaces on all constants except the last.
  {
    auto visiting = Visiting(this, VisitorKind::kBinaryOperatorFirstConstant);
    auto operand_builder = SpanBuilder<AtomicSpanSequence>(this, *element->left_operand);
    TreeVisitor::OnConstant(element->left_operand);
  }

  auto visiting = Visiting(this, VisitorKind::kBinaryOperatorSecondConstant);
  auto operand_builder = SpanBuilder<AtomicSpanSequence>(this, *element->right_operand);
  TreeVisitor::OnConstant(element->right_operand);
}

void SpanSequenceTreeVisitor::OnCompoundIdentifier(
    const std::unique_ptr<raw::CompoundIdentifier>& element) {
  auto visiting = Visiting(this, VisitorKind::kCompoundIdentifier);
  auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnCompoundIdentifier(element);
}

void SpanSequenceTreeVisitor::OnConstant(const std::unique_ptr<raw::Constant>& element) {
  auto visiting = Visiting(this, VisitorKind::kConstant);
  auto span_builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnConstant(element);
}

void SpanSequenceTreeVisitor::OnConstDeclaration(
    const std::unique_ptr<raw::ConstDeclaration>& element) {
  auto visiting = Visiting(this, VisitorKind::kConstDeclaration);
  auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);

  // TODO(fxbug.dev/73507): format attributes as well.

  // We need a separate scope for these two nodes, as they are meant to be their own
  // DivisibleSpanSequence, but no raw AST node or visitor exists for grouping them.
  {
    auto lhs_builder = SpanBuilder<DivisibleSpanSequence>(this, element->start_);

    // Keep the "const" keyword atomic with the name of the declaration.
    {
      auto name_builder = SpanBuilder<AtomicSpanSequence>(this, *element->identifier);
      OnIdentifier(element->identifier);
    }

    // Similarly, keep the type constructor atomic as well.
    {
      auto& type_ctor_new = std::get<std::unique_ptr<raw::TypeConstructorNew>>(element->type_ctor);
      auto type_ctor_new_builder = SpanBuilder<AtomicSpanSequence>(this, *type_ctor_new);
      OnTypeConstructor(element->type_ctor);
    }
    AddSpacesBetweenChildren(building_.top());
  }

  OnConstant(element->constant);
  AddSpacesBetweenChildren(building_.top());
}

void SpanSequenceTreeVisitor::OnFile(const std::unique_ptr<raw::File>& element) {
  auto visiting = Visiting(this, VisitorKind::kFile);
  building_.push(std::vector<std::unique_ptr<SpanSequence>>());

  DeclarationOrderTreeVisitor::OnFile(element);

  if (!uningested_.empty()) {
    auto footer = IngestUntilEndOfFile();
    if (footer.has_value())
      building_.top().push_back(std::move(footer.value()));
  }
}

void SpanSequenceTreeVisitor::OnIdentifier(const std::unique_ptr<raw::Identifier>& element) {
  auto visiting = Visiting(this, VisitorKind::kIdentifier);
  if (IsInsideOf(VisitorKind::kCompoundIdentifier)) {
    auto builder = TokenBuilder(this, *element, false);
    TreeVisitor::OnIdentifier(element);
  } else {
    auto span_builder = SpanBuilder<AtomicSpanSequence>(this, *element);
    auto token_builder = TokenBuilder(this, *element, false);
    TreeVisitor::OnIdentifier(element);
  }
}

void SpanSequenceTreeVisitor::OnLiteral(const std::unique_ptr<raw::Literal>& element) {
  auto visiting = Visiting(this, VisitorKind::kLiteral);
  auto trailing_space = IsInsideOf(VisitorKind::kBinaryOperatorFirstConstant);
  auto builder = TokenBuilder(this, *element, trailing_space);
  TreeVisitor::OnLiteral(element);
}

void SpanSequenceTreeVisitor::OnIdentifierConstant(
    const std::unique_ptr<raw::IdentifierConstant>& element) {
  auto visiting = Visiting(this, VisitorKind::kIdentifierConstant);
  TreeVisitor::OnIdentifierConstant(element);
}

void SpanSequenceTreeVisitor::OnLibraryDecl(const std::unique_ptr<raw::LibraryDecl>& element) {
  auto visiting = Visiting(this, VisitorKind::kLibraryDecl);
  auto builder = StatementBuilder<AtomicSpanSequence>(this, *element,
                                                      SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnLibraryDecl(element);
}

void SpanSequenceTreeVisitor::OnLiteralConstant(
    const std::unique_ptr<raw::LiteralConstant>& element) {
  auto visiting = Visiting(this, VisitorKind::kLiteralConstant);
  TreeVisitor::OnLiteralConstant(element);
}

void SpanSequenceTreeVisitor::OnNamedLayoutReference(
    const std::unique_ptr<raw::NamedLayoutReference>& element) {
  auto visiting = Visiting(this, VisitorKind::kNamedLayoutReference);
  auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnNamedLayoutReference(element);
}

void SpanSequenceTreeVisitor::OnTypeConstructorNew(
    const std::unique_ptr<raw::TypeConstructorNew>& element) {
  auto visiting = Visiting(this, VisitorKind::kTypeConstructorNew);
  auto builder = SpanBuilder<AtomicSpanSequence>(this, *element);
  TreeVisitor::OnTypeConstructorNew(element);
}

void SpanSequenceTreeVisitor::OnUsing(const std::unique_ptr<raw::Using>& element) {
  auto visiting = Visiting(this, VisitorKind::kUsing);
  auto builder = StatementBuilder<DivisibleSpanSequence>(
      this, *element, SpanSequence::Position::kNewlineUnindented);
  TreeVisitor::OnUsing(element);
  AddSpacesBetweenChildren(building_.top());
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

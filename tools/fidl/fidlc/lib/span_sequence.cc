// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/span_sequence.h"

#include "fidl/raw_ast.h"

namespace fidl::fmt {

namespace {

const size_t kIndentation = 4;
const size_t kWrappedIndentation = kIndentation * 2;

// Before printing some text after a newline, we want to make sure to indent to the proper position.
// No indentation is performed if we are not on a newline, or are on the first line of the output.
void MaybeIndentLine(size_t indentation, std::string* out) {
  if (out->empty() || out->at(out->size() - 1) == '\n')
    *out += std::string(indentation, ' ');
}

// Walks a list of SpanSequences, returning the index of the first one that is not a comment.
std::optional<size_t> FirstNonCommentChildIndex(
    const std::vector<std::unique_ptr<SpanSequence>>& list) {
  for (size_t i = 0; i < list.size(); ++i) {
    auto& item = list[i];
    if (!item->IsComment()) {
      return i;
    }
  }
  return std::nullopt;
}

// Walks a list of SpanSequences, returning the index of the last one that is not a comment.
std::optional<size_t> LastNonCommentChildIndex(
    const std::vector<std::unique_ptr<SpanSequence>>& list) {
  for (size_t i = list.size(); i-- != 0;) {
    auto& item = list[i];
    if (!item->IsComment()) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

void SpanSequence::Close() { closed_ = true; }

void TokenSpanSequence::Close() {
  if (!IsClosed()) {
    SetRequiredSize(span_.size());
    // TODO(fxbug.dev/73507): add more variants to the regex as we run across them when adding
    //  support for more raw AST node types.
    static std::regex needs_trailing_space_regex("[a-z_]+|=|:");

    if (allow_trailing_space_ && std::regex_match(std::string(span_), needs_trailing_space_regex)) {
      SetTrailingSpace(true);
    }
    SpanSequence::Close();
  }
}

std::optional<SpanSequence::Kind> TokenSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, std::string* out) const {
  if (GetLeadingBlankLines() > 0 && last_printed_kind != SpanSequence::Kind::kToken) {
    for (size_t i = 0; i < GetLeadingBlankLines(); i++) {
      *out += "\n";
    }
  }

  MaybeIndentLine(indentation, out);
  *out += std::string(span_);
  return SpanSequence::Kind::kToken;
}

void CompositeSpanSequence::AddChild(std::unique_ptr<SpanSequence> child) {
  assert(!IsClosed() && "cannot AddChild to closed AtomicSpanSequence");
  children_.push_back(std::move(child));
}

// Required size calculations take care to exclude comments, but to include all non-edge spaces, in
// their calculation.  Thus, the string `foo bar` has an inline size of 7, the same as ` foo bar  `.
// Additionally, this span, divided by a comment, has a required size of 7 as well:
//
//   foo // comment
//   bar
size_t CompositeSpanSequence::CalculateRequiredSize() const {
  size_t accumulator = 0;
  const auto last = LastNonCommentChildIndex(children_);
  for (size_t i = 0; i < children_.size(); i++) {
    auto& child = children_[i];
    switch (child->GetKind()) {
      case SpanSequence::Kind::kMultiline: {
        accumulator += child->GetRequiredSize();
        return accumulator;
      }
      default: {
        accumulator += child->GetRequiredSize();
        if (i < last.value_or(0) && child->HasTrailingSpace()) {
          accumulator += 1;
        }
        break;
      }
    }
  }
  return accumulator;
}

void CompositeSpanSequence::Close() {
  if (!IsClosed()) {
    for (auto& child : children_) {
      child->Close();
      if (child->IsComment()) {
        has_comments_ = true;
      } else {
        has_tokens_ = true;
      }
    }
    SetRequiredSize(CalculateRequiredSize());

    auto last_non_comment_index = LastNonCommentChildIndex(children_);
    if (last_non_comment_index.has_value()) {
      SetTrailingSpace(children_[last_non_comment_index.value()]->HasTrailingSpace());
    }
    SpanSequence::Close();
  }
}

void CompositeSpanSequence::CloseChildren() {
  if (!IsClosed()) {
    for (auto& child : children_) {
      child->Close();
    }
  }
}

SpanSequence* CompositeSpanSequence::GetLastChild() {
  assert(!IsClosed() && "cannot GetLastChild of closed AtomicSpanSequence");
  const auto& children = GetChildren();
  if (!children.empty()) {
    return children.at(children.size() - 1).get();
  }
  return nullptr;
}

bool CompositeSpanSequence::IsEmpty() { return children_.empty(); }

const std::vector<std::unique_ptr<SpanSequence>>& CompositeSpanSequence::GetChildren() const {
  return children_;
}

std::optional<SpanSequence::Kind> AtomicSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, std::string* out) const {
  const auto& children = GetChildren();
  const auto first = FirstNonCommentChildIndex(children);
  const auto last = LastNonCommentChildIndex(children);
  auto wrapped_indentation = indentation + (wrapped ? kWrappedIndentation : 0);
  for (size_t i = 0; i < children.size(); ++i) {
    const auto& child = children[i];
    switch (child->GetKind()) {
      case SpanSequence::Kind::kAtomic: {
        MaybeIndentLine(wrapped_indentation, out);
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);

        // If the child AtomicSpanSequence had comments, we know that it forces a wrapping, so
        // all future printing for this AtomicSpanSequence must be wrapped as well.
        if (!wrapped && child->HasComments() && child->HasTokens()) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        break;
      }
      case SpanSequence::Kind::kToken: {
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, wrapped_indentation, wrapped, out);
        break;
      }
      case SpanSequence::Kind::kInlineComment: {
        // An inline comment must always have a leading space, to properly separate it from the
        // preceding token.
        if (!out->empty() && !isspace(out->back()))
          *out += " ";
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);

        // A comment always forces the rest of the AtomicSpanSequence content to be wrapped.
        if (!wrapped) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        break;
      }
      case SpanSequence::Kind::kStandaloneComment: {
        // A standalone comment forces a newline, but its possible that the preceding token already
        // printed its trailing space.  We don't want to leave that trailing space hanging before a
        // newline, so delete the space.
        if (!out->empty() && out->at(out->size() - 1) == ' ')
          out->pop_back();

        // A standalone comment always forces the rest of the AtomicSpanSequence content to be
        // wrapped, unless that comment precedes the first non-comment token in the span.
        if (!wrapped && first.has_value() && i >= first.value()) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);
        break;
      }
      default:
        assert(false && "other comment types must not be children of AtomicSpanSequence");
    }

    // If the last printed SpanSequence was a token, and that token has declared itself to have a
    // trailing space, we print that space.  However, if this is the last non-whitespace token in
    // the current AtomicSpanSequence, this decision is delegated to its parent, so avoid printing
    // for now.
    if (child->HasTrailingSpace() && last_printed_kind == SpanSequence::Kind::kToken &&
        i < last.value_or(0)) {
      *out += " ";
    }
  }

  return last_printed_kind;
}

std::optional<SpanSequence::Kind> DivisibleSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, std::string* out) const {
  const auto& children = GetChildren();
  const auto required_size = GetRequiredSize();
  const auto last = LastNonCommentChildIndex(children);
  auto wrapped_indentation = indentation + (wrapped ? kWrappedIndentation : 0);
  auto space_available = max_col_width - wrapped_indentation;
  assert(wrapped_indentation <= max_col_width && "indentation overflow");

  if (required_size > space_available) {
    // We can't fit this DivisibleSpanSequence on a single line, either due to a lack of space, or
    // otherwise because it has a MultiSpanSequence somewhere in the middle of its child nodes,
    // which forces line breaks.
    for (size_t i = 0; i < children.size(); ++i) {
      const auto& child = children[i];
      MaybeIndentLine(wrapped_indentation, out);
      last_printed_kind = child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);
      if (i < last.value_or(0)) {
        *out += "\n";
      }
      if (i == 0 && !wrapped) {
        wrapped = true;
      }
    }

    return last_printed_kind;
  }

  // We can fit this DivisibleSpanSequence on a single line!
  // TODO(fxbug.dev/73507): this partially duplicates the code in AtomicSpanSequence::Print.
  //  Investigate using CompositeSpanSequence::Print for both cases instead.
  for (size_t i = 0; i < children.size(); ++i) {
    auto& child = children[i];
    switch (child->GetKind()) {
      case SpanSequence::Kind::kInlineComment:
      case SpanSequence::Kind::kStandaloneComment: {
        assert(false && "comments may not be children of DivisibleSpanSequence");
        break;
      }
      case SpanSequence::Kind::kAtomic:
      case SpanSequence::Kind::kDivisible: {
        MaybeIndentLine(wrapped_indentation, out);
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);

        // If the child AtomicSpanSequence had comments, we know that it forces a wrapping, so
        // all future printing for this AtomicSpanSequence must be wrapped as well.
        if (!wrapped && child->HasComments() && child->HasTokens()) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        break;
      }
      case SpanSequence::Kind::kToken: {
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);
        break;
      }
      case SpanSequence::Kind::kMultiline: {
        MaybeIndentLine(wrapped_indentation, out);
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, out);
        if (!wrapped) {
          wrapped = true;
        }
        break;
      }
    }

    // Always put spaces between the unwrapped elements of the DivisibleSpanSequence if they are
    // tokens.
    if (last_printed_kind == SpanSequence::Kind::kToken && i < last.value_or(0)) {
      *out += " ";
    }
  }

  return last_printed_kind;
}

// For MultilineSpanSequences, we only require enough space on a given line to fit the first line of
// the SpanSequence, since the rest of it will be forced onto new lines anyway.
size_t MultilineSpanSequence::CalculateRequiredSize() const {
  const auto& children = GetChildren();
  const auto first = FirstNonCommentChildIndex(children);
  if (first.has_value()) {
    return children[first.value()]->GetRequiredSize();
  }
  return 0;
}

std::optional<SpanSequence::Kind> MultilineSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, std::string* out) const {
  for (auto& child : GetChildren()) {
    auto child_indentation = indentation;
    if (child->GetPosition() != SpanSequence::Position::kDefault) {
      if (last_printed_kind == SpanSequence::Kind::kToken) {
        *out += "\n";
      }
      if (child->GetPosition() == SpanSequence::Position::kNewlineIndented) {
        child_indentation += kIndentation;
      }
      MaybeIndentLine(child_indentation, out);
    }
    last_printed_kind =
        child->Print(max_col_width, last_printed_kind, child_indentation, false, out);
  }

  return last_printed_kind;
}

std::optional<SpanSequence::Kind> InlineCommentSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, std::string* out) const {
  *out += std::string(comment_) + "\n";
  return SpanSequence::Kind::kInlineComment;
}

// Consider this standalone comment:
//
//   // line 1
//   //
//   // line 3
//
//   // line 5
//
// Lines 1, 3, and 5 are stored in the "lines_" vector of StandaloneCommentSpanSequence as
// string_views like `// line N`.  Line 2 is stored as `//`, while line 4 (technically totally
// absent, but still a connecting part of the comment block) is stored as an empty string_view.
void StandaloneCommentSpanSequence::AddLine(const std::string_view line,
                                            size_t leading_blank_lines) {
  if (IsClosed()) {
    assert(false && "cannot AddLine to closed StandaloneCommentSpanSequence");
  }
  while (leading_blank_lines > 0) {
    lines_.emplace_back(std::string_view());
    leading_blank_lines--;
  }
  lines_.push_back(line);
}

std::optional<SpanSequence::Kind> StandaloneCommentSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, std::string* out) const {
  const auto wrapped_indentation = indentation + (wrapped ? kWrappedIndentation : 0);
  if (last_printed_kind.has_value()) {
    if (last_printed_kind == SpanSequence::Kind::kToken) {
      *out += "\n";
    }
    for (size_t i = 0; i < GetLeadingBlankLines(); i++) {
      *out += "\n";
    }
  }

  for (auto& line : lines_) {
    if (!line.empty()) {
      *out += std::string(wrapped_indentation, ' ');
      *out += line;
    }
    *out += "\n";
  }

  return SpanSequence::Kind::kStandaloneComment;
}

}  // namespace fidl::fmt

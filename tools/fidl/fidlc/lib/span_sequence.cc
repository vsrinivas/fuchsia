// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/span_sequence.h"

#include <zircon/assert.h>

#include <string>

namespace fidl::fmt {

namespace {

const size_t kIndentation = 4;
const size_t kWrappedIndentation = kIndentation * 2;

// If the last printed entity was a StandaloneCommentSpanSequence, we need to make sure we print the
// leading_blank_lines_ of whatever SpanSequence we are currently looking at.
void MaybeAddBlankLinesAfterStandaloneComment(const SpanSequence* printing,
                                              std::optional<SpanSequence::Kind> last_printed_kind,
                                              std::string* out) {
  if (printing->GetLeadingBlankLines() > 0 &&
      last_printed_kind == SpanSequence::Kind::kStandaloneComment) {
    while (!out->empty() && out->back() == ' ')
      out->pop_back();
    for (size_t i = 0; i < printing->GetLeadingBlankLines(); i++)
      *out += "\n";
  }
}

// Before printing some text after a newline, we want to make sure to indent to the proper position.
// No indentation is performed if we are not on a newline, or are on the first line of the output.
void MaybeIndentLine(size_t indentation, size_t outdentation, std::string* out) {
  if (out->empty() || out->back() == '\n') {
    // It may be tempting to write this as:
    //
    //   *out += std::string(indentation > outdentation ? indentation - outdentation : 0, ' ');
    //
    // and omit the deleting while-clause below, but this will introduce a subtle bug.  It is
    // usually the case that the SpanSequence being outdented is actually the first child of the
    // SpanSequence being indented, resulting in two calls like:
    //
    //   MaybeIndentLine(N, 0, out); // For the parent.
    //   MaybeIndentLine(0, N, out); // For the first child.
    //
    // Using the formulation above, this would result in no outdentation being applied.
    *out += std::string(indentation, ' ');
  }
  while (outdentation > 0 && !out->empty() && out->back() == ' ') {
    out->pop_back();
    outdentation -= 1;
  }
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
    SpanSequence::Close();
  }
}

std::optional<SpanSequence::Kind> TokenSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const {
  MaybeAddBlankLinesAfterStandaloneComment(this, last_printed_kind, out);
  MaybeIndentLine(indentation, GetOutdentation(), out);
  *out += std::string(span_);
  return SpanSequence::Kind::kToken;
}

void CompositeSpanSequence::AddChild(std::unique_ptr<SpanSequence> child) {
  ZX_ASSERT_MSG(!IsClosed(), "cannot AddChild to closed AtomicSpanSequence");
  children_.push_back(std::move(child));
}

// Required size calculations take care to exclude comments, but to include all non-edge spaces, in
// their calculation.  Thus, the string `foo bar` has an inline size of 7, the same as ` foo bar  `.
// Additionally, this span, divided by a comment, has a required size of 7 as well:
//
//   foo // comment
//   bar
size_t CompositeSpanSequence::CalculateRequiredSize() const {
  size_t required_size = 0;
  const auto last = LastNonCommentChildIndex(children_);
  for (size_t i = 0; i < children_.size(); i++) {
    auto& child = children_[i];
    switch (child->GetKind()) {
      case SpanSequence::Kind::kMultiline: {
        required_size += child->GetRequiredSize();
        return required_size;
      }
      default: {
        required_size += child->GetRequiredSize();
        if (i < last.value_or(0) && child->HasTrailingSpace()) {
          required_size += 1;
        }
        break;
      }
    }
  }
  return required_size;
}

void CompositeSpanSequence::Close() {
  CloseChildren();

  if (!IsClosed()) {
    // If the first child is a token, delete its leading new lines value and hoist it up to the
    // parent that is currently being closed.  This prevents duplication of leading new lines, as
    // the leading new new lines for a composite span are now always the parent's responsibility,
    // not that of the first child.
    auto first_non_comment_index = FirstNonCommentChildIndex(children_);
    if (first_non_comment_index.has_value() && first_non_comment_index == 0) {
      SetLeadingBlankLines(GetLeadingBlankLines() + children_[0]->GetLeadingBlankLines());
      children_[0]->SetLeadingBlankLines(0);
    }

    // If the last child is a token with a trailing space, reset its trailing space boolean and
    // set that of the parent (ie, the SpanSequence currently being closed).  This prevents
    // duplication of trailing spaces, as the trailing space for a composite span is now always the
    // parent's responsibility, not that of the last child.
    auto last_non_comment_index = LastNonCommentChildIndex(children_);
    if (last_non_comment_index.has_value() && last_non_comment_index == children_.size() - 1 &&
        children_[last_non_comment_index.value()]->HasTrailingSpace()) {
      SetTrailingSpace(true);
      children_[last_non_comment_index.value()]->SetTrailingSpace(false);
    }

    SetRequiredSize(CalculateRequiredSize());
    SpanSequence::Close();
  }
}

void CompositeSpanSequence::CloseChildren() {
  if (!IsClosed()) {
    for (size_t i = 0; i < children_.size(); ++i) {
      const auto& child = children_[i];
      if (!child->IsClosed())
        child->Close();

      switch (child->GetKind()) {
        case SpanSequence::Kind::kToken: {
          has_tokens_ = true;
          break;
        }
        case SpanSequence::Kind::kInlineComment: {
          has_non_leading_comments_ = true;
          break;
        }
        case SpanSequence::Kind::kStandaloneComment: {
          if (i > 0)
            has_non_leading_comments_ = true;
          break;
        }
        default: {
          if (child->HasNonLeadingComments())
            has_non_leading_comments_ = true;
          if (child->HasTokens())
            has_tokens_ = true;
        }
      }
    }
  }
}

SpanSequence* CompositeSpanSequence::GetLastChild() {
  ZX_ASSERT_MSG(!IsClosed(), "cannot GetLastChild of closed AtomicSpanSequence");
  const auto& children = GetChildren();
  if (!children.empty()) {
    return children.at(children.size() - 1).get();
  }
  return nullptr;
}

bool CompositeSpanSequence::IsEmpty() { return children_.empty(); }

std::vector<std::unique_ptr<SpanSequence>>& CompositeSpanSequence::EditChildren() {
  return children_;
}

const std::vector<std::unique_ptr<SpanSequence>>& CompositeSpanSequence::GetChildren() const {
  return children_;
}

std::optional<SpanSequence::Kind> AtomicSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const {
  const auto& children = GetChildren();
  const auto first = FirstNonCommentChildIndex(children);
  const auto last = LastNonCommentChildIndex(children);
  auto wrapped_indentation = indentation + (wrapped ? kWrappedIndentation : 0);

  MaybeAddBlankLinesAfterStandaloneComment(this, last_printed_kind, out);

  for (size_t i = 0; i < children.size(); ++i) {
    const auto& child = children[i];
    const AdjacentIndents indents = AdjacentIndents(
        i == 0 && adjacent_indents.prev, i == children.size() - 1 && adjacent_indents.next);

    switch (child->GetKind()) {
      case SpanSequence::Kind::kAtomic:
      case SpanSequence::Kind::kDivisible: {
        MaybeIndentLine(wrapped_indentation, child->GetOutdentation(), out);
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);

        // If the child AtomicSpanSequence had comments, we know that it forces a wrapping, so
        // all future printing for this AtomicSpanSequence must be wrapped as well.
        if (!wrapped && child->HasNonLeadingComments() && child->HasTokens()) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        break;
      }
      case SpanSequence::Kind::kToken: {
        last_printed_kind = child->Print(max_col_width, last_printed_kind, wrapped_indentation,
                                         wrapped, indents, out);
        break;
      }
      case SpanSequence::Kind::kInlineComment: {
        // An inline comment must always have a leading space, to properly separate it from the
        // preceding token.
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);

        // A comment always forces the rest of the AtomicSpanSequence content to be wrapped if its
        // between non-comment children.
        if (!wrapped && i >= first.value_or(children.size()) && i < last.value_or(0)) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        break;
      }
      case SpanSequence::Kind::kStandaloneComment: {
        // Special case: If this is the very last child in the AtomicSpanSequence, and the next
        // token after this AtomicSpanSequence will be indented, we want to make sure to indent this
        // child StandaloneCommentSpanSequence as well.  If we did not do this check, we would get
        // something like the following (AtomicSpanSequence bounded with «», its children with ⸢⸥):
        //
        //     type MyStruct = «⸢struct⸥ ⸢{⸥
        //     ⸢// You should have indented me!⸥»
        //         field1 ...
        if (!wrapped && indents.HasAdjacentIndent()) {
          indentation += kIndentation;
          last_printed_kind =
              child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);
          break;
        }

        // A standalone comment always forces the rest of the AtomicSpanSequence content to be
        // wrapped, unless that comment precedes the first non-comment token in the span.
        if (!wrapped && i >= first.value_or(children.size())) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);
        break;
      }
      case SpanSequence::Kind::kMultiline: {
        MaybeIndentLine(wrapped_indentation, child->GetOutdentation(), out);
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);
        if (!wrapped) {
          wrapped = true;
        }
        break;
      }
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
    size_t indentation, bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const {
  const auto& children = GetChildren();
  const auto required_size = GetRequiredSize();
  const auto last = LastNonCommentChildIndex(children);
  auto wrapped_indentation = indentation + (wrapped ? kWrappedIndentation : 0);
  auto space_available = max_col_width - wrapped_indentation;
  ZX_ASSERT_MSG(wrapped_indentation <= max_col_width, "indentation overflow");

  MaybeAddBlankLinesAfterStandaloneComment(this, last_printed_kind, out);

  // See the `NoPointlessWrapping` test case in `formatter_tests.cc` for an illustrative example
  // of why the conditional after `&&` exists, but the brief explanation is that indenting in cases
  // where there are only two elements in the DivisibleSpanSequence and the first one is very short
  // (<8 chars) is counterproductive, producing output like:
  //
  // type MyStruct = {
  //     a
  //             MyVeryVery...VeryLongTypeName;
  // };
  if (required_size > space_available &&
      (children.size() > 2 || children[0]->GetRequiredSize() >= kWrappedIndentation)) {
    // We can't fit this DivisibleSpanSequence on a single line, either due to a lack of space, or
    // otherwise because it has a MultiSpanSequence somewhere in the middle of its child nodes,
    // which forces line breaks.
    for (size_t i = 0; i < children.size(); ++i) {
      const auto& child = children[i];
      const AdjacentIndents indents = AdjacentIndents(
          i == 0 && adjacent_indents.prev, i == children.size() - 1 && adjacent_indents.next);

      MaybeIndentLine(wrapped_indentation, child->GetOutdentation(), out);
      last_printed_kind =
          child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);
      if (i < last.value_or(0)) {
        *out += "\n";
      }
      if (i == 0 && !wrapped) {
        wrapped = true;
        wrapped_indentation += kWrappedIndentation;
      }
    }

    return last_printed_kind;
  }

  // We can fit this DivisibleSpanSequence on a single line!
  for (size_t i = 0; i < children.size(); ++i) {
    auto& child = children[i];
    const AdjacentIndents indents = AdjacentIndents(
        i == 0 && adjacent_indents.prev, i == children.size() - 1 && adjacent_indents.next);
    switch (child->GetKind()) {
      case SpanSequence::Kind::kInlineComment:
      case SpanSequence::Kind::kStandaloneComment: {
        ZX_PANIC("comments may not be children of DivisibleSpanSequence");
      }
      case SpanSequence::Kind::kAtomic:
      case SpanSequence::Kind::kDivisible: {
        MaybeIndentLine(wrapped_indentation, child->GetOutdentation(), out);
        last_printed_kind =
            child->Print(max_col_width, last_printed_kind, indentation, wrapped, indents, out);

        // In certain weird circumstances (ie, comments placed in unexpected areas), a child
        // AtomicSpanSequence may start with an inline comment.  If this is the case, make sure to
        // wrap the rest of this SpanSequence.
        auto as_composite = static_cast<CompositeSpanSequence*>(child.get());
        auto starts_with_inline = false;
        if (!as_composite->IsEmpty()) {
          const auto& inner_children = as_composite->GetChildren();
          starts_with_inline = inner_children[0]->GetKind() == SpanSequence::Kind::kInlineComment;
        }

        // If the child AtomicSpanSequence had comments, we know that it forces a wrapping, so
        // all future printing for this AtomicSpanSequence must be wrapped as well.
        if (!wrapped && child->HasNonLeadingComments() &&
            (child->HasTokens() || starts_with_inline)) {
          wrapped = true;
          wrapped_indentation += kWrappedIndentation;
        }
        break;
      }
      case SpanSequence::Kind::kToken: {
        last_printed_kind = child->Print(max_col_width, last_printed_kind, indentation, wrapped,
                                         adjacent_indents, out);
        break;
      }
      case SpanSequence::Kind::kMultiline: {
        MaybeIndentLine(wrapped_indentation, child->GetOutdentation(), out);
        last_printed_kind = child->Print(max_col_width, last_printed_kind, indentation, wrapped,
                                         adjacent_indents, out);
        if (!wrapped) {
          wrapped = true;
        }
        break;
      }
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
    size_t indentation, bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const {
  const auto& children = GetChildren();
  const auto first_non_comment_index = FirstNonCommentChildIndex(children);
  for (size_t i = 0; i < children.size(); ++i) {
    const auto& child = children[i];
    auto child_indentation = indentation;
    const bool prev_token_is_indented =
        i == 0 ? adjacent_indents.prev
               : (children[i - 1]->GetPosition() == SpanSequence::Position::kNewlineIndented &&
                  child->GetPosition() != SpanSequence::Position::kNewlineIndented);
    const bool next_token_is_indented =
        i == children.size() - 1
            ? adjacent_indents.next
            : (children[i + 1]->GetPosition() == SpanSequence::Position::kNewlineIndented &&
               child->GetPosition() != SpanSequence::Position::kNewlineIndented);

    if (child->GetPosition() != SpanSequence::Position::kDefault) {
      if (last_printed_kind == SpanSequence::Kind::kToken) {
        // Omit one of the blank lines in cases where a MultilineSpanSequence is the first child of
        // another MultilineSpanSequence, meaning that the first newline has already been printed.
        auto blanks = child->GetLeadingBlankLines();
        for (size_t n = !out->empty() && out->back() == '\n' ? 1 : 0; n <= blanks; n++) {
          *out += "\n";
        }
      }
      if (child->GetPosition() == SpanSequence::Position::kNewlineIndented) {
        child_indentation += kIndentation;
      }
      if (child->GetKind() != SpanSequence::Kind::kMultiline) {
        MaybeIndentLine(child_indentation, child->GetOutdentation(), out);
      }
    }

    const bool keep_wrapped = wrapped && i == first_non_comment_index.value_or(children.size());
    last_printed_kind =
        child->Print(max_col_width, last_printed_kind, child_indentation, keep_wrapped,
                     AdjacentIndents(prev_token_is_indented, next_token_is_indented), out);
  }

  return last_printed_kind;
}

void CommentSpanSequence::Close() {
  SetTrailingSpace(false);
  SpanSequence::Close();
}

std::optional<SpanSequence::Kind> InlineCommentSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const {
  // Remove all whitespace before the inline comment, then add exactly one space back.
  while (!out->empty() && (out->back() == ' ' || out->back() == '\n'))
    out->pop_back();
  if (!out->empty())
    *out += " ";

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
  ZX_ASSERT_MSG(!IsClosed(), "cannot AddLine to closed StandaloneCommentSpanSequence");
  while (leading_blank_lines > 0) {
    lines_.emplace_back();
    leading_blank_lines--;
  }
  lines_.push_back(line);
}

std::optional<SpanSequence::Kind> StandaloneCommentSpanSequence::Print(
    const size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind,
    size_t indentation, bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const {
  // A standalone comment forces a newline, but its possible that the preceding token already
  // printed its trailing space(s), or otherwise we've already indented this line.  We don't want to
  // leave that trailing whitespace hanging before a newline, so let's delete the extra space(s).
  // Deleting whitespace we've already printed is a bit clumsy, but this is the only place where we
  // "undo" writes in this manner, and doing it this way allows us to keep the printer stateless.
  while (!out->empty() && out->back() == ' ')
    out->pop_back();

  const auto wrapped_indentation = indentation + (wrapped ? kWrappedIndentation : 0);
  if (last_printed_kind.has_value()) {
    // Make sure we start the comment on a newline, if one has not already been added to the output.
    if (last_printed_kind == SpanSequence::Kind::kToken && !out->empty() && out->back() != '\n') {
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

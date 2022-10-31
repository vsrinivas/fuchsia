// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SPAN_SEQUENCE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SPAN_SEQUENCE_H_

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fidl::fmt {

// Tracks whether the line a particular token finds itself on is indented relative to either its
// immediate predecessor (in which case the `prev` property is true) or follower (in which case the
// `next` property is true). This is done to ensure that comments are always properly "aligned,"
// meaning that they match the indentation of either their previous or following line, whichever is
// greater. Consider the following text:
//
// type MyStruct = struct {
//   // C1
//   a bool;
//   // C2
//   b bool;
//   // C3
// }
//
// For `C1`, we need to know that the line immediately following it has a greater indentation so
// that we may indent the comment properly. Similarly, for `C3`, we need to know that the preceding
// line has the greater indentation. However, for `C2`, we know that the preceding and following
// lines have equal indentation, an indentation depth which `C2` is expected to match.
class AdjacentIndents final {
 public:
  AdjacentIndents(const bool prev, const bool next) : prev(prev), next(next) {}
  const bool prev = false;
  const bool next = false;

  bool HasAdjacentIndent() const { return prev || next; }
};

// A SpanSequence represents some source text in the FIDL file being formatted.  Depending on its
// kind, the SpanSequence encodes how that block of text should be handled by the printer.  For
// example, a DivisibleSpanSequence should be broken up into its constituent parts and wrapped if it
// overflows, while an InlineCommentSpanSequence should be inserted wherever it appears and always
// cause all source in the statement after it to be wrapped.
class SpanSequence {
 public:
  enum struct Kind {
    kAtomic,
    kDivisible,
    kInlineComment,
    kMultiline,
    kStandaloneComment,
    kToken,
  };

  // Any SpanSequence can carry this property, but it only affects the output if that SpanSequence
  // is a child of a MultilineSpanSequence.  It is used by MultilineSpanSequence to decide how each
  // of its children is indented. For example, consider this formatted and annotated type
  // declaration:
  //
  // type MyStruct = struct {   // <- kNewlineUnindented
  //   a bool;                  // <- kNewlineIndented
  //   // My trailing comment.  // <- kNewlineAligned
  // };                         // <- kNewlineUnindented
  //
  // The meaning of kNewlineIndented and kNewlineUnindented is obvious: the former is always
  // indented, the latter is never is.  Next, kNewlineAligned is used to indicate that this line
  // should take the indentation of the sibling immediately before or after it, whichever is
  // greater. Finally, kDefault means that we do not want a newline at all.
  enum struct Position {
    kDefault,
    kNewlineAligned,
    kNewlineIndented,
    kNewlineUnindented,
  };

  explicit SpanSequence(Kind kind, Position position, size_t leading_blank_lines = 0)
      : kind_(kind), position_(position), leading_blank_lines_(leading_blank_lines) {}

  virtual ~SpanSequence() = default;

  virtual void Close();

  // What's a "non-leading" comment, you ask?  It's any comment that is not both a StandaloneComment
  // and the first leaf token in this SpanSequence's tree.  These should always be treated as
  // standalone entities that never affect wrapping, so this method ignores them when it asks "are
  // there comments contained in this SpanSequence?"
  virtual bool HasNonLeadingComments() const = 0;
  virtual bool HasTokens() const = 0;
  virtual bool IsComment() const = 0;
  virtual bool IsComposite() const = 0;

  // The printer keeps track of the last token kind to be printed.  Since CompositeSpanSequences are
  // merely containers for the "printable" token kinds (kToken, kInlineComment, kStandaloneComment),
  // this return kind may not be for any class inhering from CompositeSpanSequence (ie, kAtomic,
  // kDivisible, and kMultiline are not allowed).
  //
  // The function takes the following arguments:
  //   * max_col_width: The maximum width of a column in our file.  This is passed in via the top
  //       level Print call, and should not be changed as it is recursed through the SpanSequence
  //       tree.
  //   * last_printed_kind: The kind (kToken, kStandaloneComment, or kInlineComment) of the last
  //       text added to the output.
  //   * indentation: the number of spaces text appearing on newlines should be indented.
  //   * wrapped: whether or not the last output line is already wrapped.  It is expected that this
  //       value has NOT been added to the `indentation` value.  That is, if an unwrapped line has
  //       `indentation=4,wrapped=false`, the wrapped line should be `indentation=4,wrapped=true`.
  //   * is_next_sibling_indented: this is a form of lookahead that notes whether the next bit of
  //       text to be added to the output AFTER this SpanSequence has finished printing will be on
  //       an indented newline.  This is important to note because we want StandaloneComments to
  //       be aligned to the indentation of either their preceding or following line, whichever is
  //       greater.  Without this argument, we would get output like:
  //
  //         type MyStruct = struct {
  //         // Uh-oh, I wasn't indented properly!
  //             foo bool;
  //         };
  //
  //   * out: a pointer to the output string being built by this printer.
  virtual std::optional<SpanSequence::Kind> Print(
      size_t max_col_width, std::optional<SpanSequence::Kind> last_printed_kind, size_t indentation,
      bool wrapped, AdjacentIndents adjacent_indents, std::string* out) const = 0;

  size_t GetLeadingBlankLines() const { return leading_blank_lines_; }
  size_t GetOutdentation() const { return outdentation_; }
  size_t GetRequiredSize() const { return required_size_; }
  Kind GetKind() const { return kind_; }
  Position GetPosition() const { return position_; }
  bool HasTrailingSpace() const { return trailing_space_; }
  bool IsClosed() const { return closed_; }
  void SetLeadingBlankLines(size_t leading_blanks) { leading_blank_lines_ = leading_blanks; }
  void SetOutdentation(size_t outdentation) { outdentation_ = outdentation; }
  void SetRequiredSize(size_t required_size) { required_size_ = required_size; }
  void SetTrailingSpace(bool trailing_space) { trailing_space_ = trailing_space; }

 private:
  const enum Kind kind_;
  const enum Position position_;

  // A "closed" SpanSequence can no longer be modified.  When the Close() method is called, the
  // required_size_ and trailing_space_ members are calculated, and may then be accessed by
  // downstream functions like the printer.
  bool closed_ = false;

  // Tracks the number of leading new lines to print before this SpanSequence is added to the
  // printer's output string,
  size_t leading_blank_lines_;

  // The number of spaces to remove from the indentation when printing this SpanSequence.  As of
  // now, it is only used for the purpose of vertically aligning ordinaled layout members, like so:
  //
  //   type MyTable = table {
  //     1: reserved;
  //     // ...
  //    10: reserved;
  //     // ...
  //   100: reserved;
  //     // etc...
  //   };
  size_t outdentation_ = 0;

  // Tracks how many characters of line space are needed to render this SpanSequence without
  // dividing it.  For example, if we have the DivisibleSpanSequence:
  //
  //   |------------------40------------------|
  //   Method(MyLongRequestName) -> (MyLongResponseName);
  //   |-----------------------| |----------------------|
  //   |-----------------------50-----------------------|
  //
  // we can see that it's required_size_ of 50 is greater than the available line width of 40, so it
  // must be split.
  size_t required_size_ = 0;

  // Tracks whether or not we would like to add a trailing space after this SpanSequence, though it
  // does not strictly guarantee that such a space will appear in the final output.  For example, if
  // we have a TokenSpanSequence of `=`, picked from a FIDL statement like:
  //
  //   type MyStruct = struct {};
  //
  // we want to make sure the token gets rendered with a space after it in the common case.
  // However, in certain cases, the SpanSequence immediately following this one may modify its
  // behavior.  In the example above, if the `=` were immediately followed by a
  // StandaloneCommentSpanSequence, we would want to avoid printing the trailing space, like so:
  //
  //   type MyStruct =
  //       // My oddly placed comment.
  //       struct {};
  bool trailing_space_ = false;
};

// Each TokenSpanSequence points to an underlying string_view representing exactly one token from
// the original source file being formatted. TokenSpanSequence is the only leaf node representing
// source code in the SpanSequence tree.
class TokenSpanSequence final : public SpanSequence {
 public:
  explicit TokenSpanSequence(const std::string_view span, size_t leading_blank_lines = 0)
      : SpanSequence(SpanSequence::Kind::kToken, SpanSequence::Position::kDefault,
                     leading_blank_lines),
        span_(span) {}

  void Close() override;
  bool HasNonLeadingComments() const override { return false; }
  bool HasTokens() const override { return false; }
  bool IsComment() const override { return false; }
  bool IsComposite() const override { return false; }
  std::optional<SpanSequence::Kind> Print(size_t max_col_width,
                                          std::optional<SpanSequence::Kind> last_printed_kind,
                                          size_t indentation, bool wrapped,
                                          AdjacentIndents adjacent_indents,
                                          std::string* out) const override;

 private:
  const std::string_view span_;
};

// A CompositeSpanSequence is a base class for all branch nodes in the SpanSequence tree
// representing the source file.  Any SpanSequence kind may be a child of a CompositeSpanSequence.
//
// This class should not be instantiated on its own - use one of the derived classes (Atomic,
// Divisible, or Multiline) instead.
class CompositeSpanSequence : public SpanSequence {
 protected:
  explicit CompositeSpanSequence(Kind kind, Position position, size_t leading_blank_lines)
      : SpanSequence(kind, position, leading_blank_lines),
        has_non_leading_comments_(false),
        has_tokens_(false) {}
  explicit CompositeSpanSequence(Kind kind, std::vector<std::unique_ptr<SpanSequence>> children,
                                 Position position, size_t leading_blank_lines)
      : SpanSequence(kind, position, leading_blank_lines),
        children_(std::move(children)),
        has_non_leading_comments_(false),
        has_tokens_(false) {}

 public:
  void AddChild(std::unique_ptr<SpanSequence> child);
  void Close() override;
  void CloseChildren();
  std::vector<std::unique_ptr<SpanSequence>>& EditChildren();
  const std::vector<std::unique_ptr<SpanSequence>>& GetChildren() const;
  SpanSequence* GetLastChild();
  bool HasNonLeadingComments() const override { return has_non_leading_comments_; }
  bool HasTokens() const override { return has_tokens_; }
  bool IsComment() const override { return false; }
  bool IsComposite() const override { return true; }
  bool IsEmpty();

  virtual size_t CalculateRequiredSize() const;

 private:
  std::vector<std::unique_ptr<SpanSequence>> children_;
  bool has_non_leading_comments_;
  bool has_tokens_;
};

// Wrapping of AtomicSpanSequences must never occur, except when comments are encountered, in which
// case it MUST always occur immediately after each inline comment, and immediately before and after
// each standalone comment seen.  For example, if the children of some AtomicSpanSequence are:
//
//   «Word»,«Word»,«InlineComment»,«Word»,«Word»,«StandaloneComment»,«Word»
//
// When printed, it should look like:
//
//   «Word» «Word» «InlineComment»  <- note wrapping after Inline
//       «Word» «Word»
//       «StandaloneComment»        <- note wrapping before and after Standalone
//       «Word»
//
// For a more concrete example, we can look at library declarations, which are ingested into
// AtomicSpanSequences.  This means that the following unformatted library declaration is must not
// be wrapped, even if it exceeds the allowed column width:
//
//   |------------------40------------------|
//   library my.overlong.severely.overflowing.name;
//
// However, when an inline comment is added to the (still unformatted) library, we must respect it:
//
//   |------------------40------------------|
//   library my.overlong.severely // My poorly placed comment.
//   .overflowing.name;
//
// So the above gets formatted to:
//
//   |------------------40------------------|
//   library my.overlong.severely // My poorly placed comment.
//       .overflowing.name;
class AtomicSpanSequence final : public CompositeSpanSequence {
 public:
  explicit AtomicSpanSequence(Position position = SpanSequence::Position::kDefault,
                              size_t leading_blank_lines = 0)
      : CompositeSpanSequence(SpanSequence::Kind::kAtomic, position, leading_blank_lines) {}
  explicit AtomicSpanSequence(std::vector<std::unique_ptr<SpanSequence>> children,
                              Position position = SpanSequence::Position::kDefault,
                              size_t leading_blank_lines = 0)
      : CompositeSpanSequence(SpanSequence::Kind::kAtomic, std::move(children), position,
                              leading_blank_lines) {}

  std::optional<SpanSequence::Kind> Print(size_t max_col_width,
                                          std::optional<SpanSequence::Kind> last_printed_kind,
                                          size_t indentation, bool wrapped,
                                          AdjacentIndents adjacent_indents,
                                          std::string* out) const override;
};

// DivisibleSpanSequences represent multi-token FIDL that we would like to see kept as a single line
// if space allows, but are willing to split into multiple wrapped lines if necessary.  For example,
// consider the following method signature:
//
//   |------------------40------------------|
//   DoFoo(MyRequest) -> (MyResponse) error uint32;
//   [--------------| |-------------| |-----------|
//
// Uh-oh, looks like its too big for the column width we have available!  Unlike an
// AtomicSpanSequence, which would just force its way into this space as a single (overflowing)
// line, we can split a DivisibleSpanSequence as follows (note that double indentation only occurs
// after the first line):
//
//   DoFoo(MyRequest)
//       -> (MyResponse)
//       error uint32;
class DivisibleSpanSequence final : public CompositeSpanSequence {
 public:
  explicit DivisibleSpanSequence(std::vector<std::unique_ptr<SpanSequence>> children,
                                 Position position = SpanSequence::Position::kDefault,
                                 size_t leading_blank_lines = 0)
      : CompositeSpanSequence(SpanSequence::Kind::kDivisible, std::move(children), position,
                              leading_blank_lines) {}

  std::optional<SpanSequence::Kind> Print(size_t max_col_width,
                                          std::optional<SpanSequence::Kind> last_printed_kind,
                                          size_t indentation, bool wrapped,
                                          AdjacentIndents adjacent_indents,
                                          std::string* out) const override;
};

// A MultilineSpanSequence is one that is spread over multiple lines by default, where each child
// has its own line, and the indentation of children is regulated by the values of their respective
// position_ members.
class MultilineSpanSequence final : public CompositeSpanSequence {
 public:
  explicit MultilineSpanSequence(std::vector<std::unique_ptr<SpanSequence>> children,
                                 Position position = SpanSequence::Position::kDefault,
                                 size_t leading_blank_lines = 0)
      : CompositeSpanSequence(SpanSequence::Kind::kMultiline, std::move(children), position,
                              leading_blank_lines) {}

  size_t CalculateRequiredSize() const override;
  std::optional<SpanSequence::Kind> Print(size_t max_col_width,
                                          std::optional<SpanSequence::Kind> last_printed_kind,
                                          size_t indentation, bool wrapped,
                                          AdjacentIndents adjacent_indents,
                                          std::string* out) const override;
};

// A CommentSpanSequence is a base class representing a comment in the FIDL file.  Comments are,
// conceptually, placed last by the pretty printing algorithm.  The entire document is formatted as
// though there are no comments (most importantly, decisions about whether or not to wrap
// DivisibleSpanSequences are made without taking any comments in those spans into account).  After
// this has been done, comments can be re-inserted adjacent to their original, pre-formatted tokens,
// with all bounding newlines preserved.
//
// It is important to note that the actual pretty printing implementation does not work as stated
// above: printing is done in a single pass, with comments ignored for the purposes of line wrapping
// calculations, but still included in the final printed output.  However, when deciding "does this
// comment look like its been placed correctly?" the above method is probably the easiest way to
// conceptualize the problem.
//
// Note that both C-style and doc comments are held in a CommentSpanSequence, and it makes no
// distinction between them.
class CommentSpanSequence : public SpanSequence {
 protected:
  explicit CommentSpanSequence(Kind kind, Position position, size_t leading_blank_lines)
      : SpanSequence(kind, position, leading_blank_lines) {}

 public:
  void Close() override;
  bool HasNonLeadingComments() const override { return false; }
  bool HasTokens() const override { return false; }
  bool IsComment() const override { return true; }
  bool IsComposite() const override { return false; }
};

// An InlineCommentSpanSequence must always occur immediately after an some other non-comment token,
// one of either TokenSpanSequence or AtomicSpanSequence.  While it does not affect layout and
// wrapping calculations (see above), it does immediately trigger a newline in whatever SpanSequence
// it is contained inside of.
//
// Note that this class DOES contain the inline comment's leading slashes, but DOES NOT contain the
// comment's trailing newline, so inserting that into the final output is the responsibility of the
// printer.
class InlineCommentSpanSequence final : public CommentSpanSequence {
 public:
  explicit InlineCommentSpanSequence(const std::string_view comment)
      : CommentSpanSequence(SpanSequence::Kind::kInlineComment, SpanSequence::Position::kDefault,
                            0),
        comment_(comment) {}

  std::optional<SpanSequence::Kind> Print(size_t max_col_width,
                                          std::optional<SpanSequence::Kind> last_printed_kind,
                                          size_t indentation, bool wrapped,
                                          AdjacentIndents adjacent_indents,
                                          std::string* out) const override;

 private:
  const std::string_view comment_;
};

// A StandaloneCommentSpanSequence represents a block of one or more comment lines in the original
// source file text, where each such line contains no source tokens preceding the starting slashes.
// Thus, these are both ingested into StandaloneCommentSpanSequences:
//
//   // My single line comment.
//   struct{};
//
//   // My two
//   // line comment.
//
// While this is not:
//
//   struct{} // My inline comment.
//
// Note that this class DOES contain the each comment line's leading slashes, but DOES NOT contain
// the comment's trailing newline, so inserting that into the final output is the responsibility of
// the printer.
class StandaloneCommentSpanSequence final : public CommentSpanSequence {
 public:
  explicit StandaloneCommentSpanSequence(size_t leading_blank_lines = 0)
      : CommentSpanSequence(SpanSequence::Kind::kStandaloneComment,
                            SpanSequence::Position::kNewlineAligned, leading_blank_lines) {}
  explicit StandaloneCommentSpanSequence(std::vector<std::string_view> lines,
                                         size_t leading_blank_lines = 0)
      : CommentSpanSequence(SpanSequence::Kind::kStandaloneComment,
                            SpanSequence::Position::kNewlineAligned, leading_blank_lines),
        lines_(std::move(lines)) {}

  void AddLine(std::string_view line, size_t leading_blank_lines = 0);
  std::optional<SpanSequence::Kind> Print(size_t max_col_width,
                                          std::optional<SpanSequence::Kind> last_printed_kind,
                                          size_t indentation, bool wrapped,
                                          AdjacentIndents adjacent_indents,
                                          std::string* out) const override;

 private:
  std::vector<std::string_view> lines_;
};

}  // namespace fidl::fmt

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SPAN_SEQUENCE_H_

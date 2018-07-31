// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "garnet/bin/zxdb/console/output_buffer.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "garnet/public/lib/fxl/strings/split_string.h"

namespace zxdb {

namespace {

// The color codes are taken from the vte 256 colorscheme, which is pretty
// common. If needed, some fallback colors could be established to support
// some old terminal scheme.

// Syntax color codes ----------------------------------------------------------

const char kNormalEscapeCode[] = "\x1b[0m";     // "[0m" = Normal.
const char kBoldEscapeCode[] = "\x1b[1m";       // "[1m" = Bold.
const char kCommentEscapeCode[] = "\x1b[2m";    // "[2m" = Faint.
const char kErrorEscapeCode[] = "\x1b[31m";     // "[31m" = Red.
const char kWarningEscapeCode[] = "\x1b[33m";   // "[33m" = Yellow.
const char kSpecialEscapeCode[] = "\x1b[34m";   // "[34m" = Blue.
const char kReversedEscapeCode[] = "\x1b[7m";   // "[7m' = Reverse video.
const char kVariableEscapeCode[] = "\x1b[36m";  // "[36m' = Cyan.

using SyntaxColorMap = std::map<Syntax, std::pair<const char*, size_t>>;
static const SyntaxColorMap& GetSyntaxColorMap() {
  static SyntaxColorMap syntax_color_map = {
   {Syntax::kHeading, {kBoldEscapeCode, sizeof(kBoldEscapeCode) - 1}},
   {Syntax::kComment, {kCommentEscapeCode, sizeof(kCommentEscapeCode) - 1}},
   {Syntax::kError, {kErrorEscapeCode, sizeof(kErrorEscapeCode) - 1}},
   {Syntax::kWarning, {kWarningEscapeCode, sizeof(kWarningEscapeCode) - 1}},
   {Syntax::kSpecial, {kSpecialEscapeCode, sizeof(kSpecialEscapeCode) - 1}},
   {Syntax::kReversed, {kReversedEscapeCode, sizeof(kReversedEscapeCode) - 1}},
   {Syntax::kVariable, {kVariableEscapeCode, sizeof(kVariableEscapeCode) - 1}},
  };

  return syntax_color_map;
}

// Background color codes ------------------------------------------------------

const char kBackgroundBlack[] = "\x1b[48;5;0m";
const char kBackgroundBlue[] = "\x1b[48;5;4m";
const char kBackgroundCyan[] = "\x1b[48;5;6m";
const char kBackgroundGray[] = "\x1b[48;5;245m";
const char kBackgroundGreen[] = "\x1b[48;5;2m";
const char kBackgroundMagenta[] = "\x1b[48;5;5m";
const char kBackgroundRed[] = "\x1b[48;5;1m";
const char kBackgroundWhite[] = "\x1b[48;5;15m";
const char kBackgroundYellow[] = "\x1b[48;5;11m";

const char kBackgroundLightBlue[] = "\x1b[48;5;45m";
const char kBackgroundLightCyan[] = "\x1b[48;5;87m";
const char kBackgroundLightGray[] = "\x1b[48;5;250m";
const char kBackgroundLightGreen[] = "\x1b[48;5;10m";
const char kBackgroundLightMagenta[] = "\x1b[48;5;170m";
const char kBackgroundLightRed[] = "\x1b[48;5;166m";
const char kBackgroundLightYellow[] = "\x1b[48;5;190m";

using BackgroundColorMap =
    std::map<TextBackgroundColor, std::pair<const char*, size_t>>;
static const BackgroundColorMap& GetBackgroundColorMap() {
  // We substract 1 from the sizeof the strings to avoid the end null char.
  static BackgroundColorMap background_color_map = {
      {TextBackgroundColor::kBlack,
       {kBackgroundBlack, sizeof(kBackgroundBlack) - 1}},
      {TextBackgroundColor::kBlue,
       {kBackgroundBlue, sizeof(kBackgroundBlue) - 1}},
      {TextBackgroundColor::kCyan,
       {kBackgroundCyan, sizeof(kBackgroundCyan) - 1}},
      {TextBackgroundColor::kGray,
       {kBackgroundGray, sizeof(kBackgroundGray) - 1}},
      {TextBackgroundColor::kGreen,
       {kBackgroundGreen, sizeof(kBackgroundGreen) - 1}},
      {TextBackgroundColor::kMagenta,
       {kBackgroundMagenta, sizeof(kBackgroundMagenta) - 1}},
      {TextBackgroundColor::kRed,
       {kBackgroundRed, sizeof(kBackgroundRed) - 1}},
      {TextBackgroundColor::kWhite,
       {kBackgroundWhite, sizeof(kBackgroundWhite) - 1}},
      {TextBackgroundColor::kYellow,
       {kBackgroundYellow, sizeof(kBackgroundYellow) - 1}},

      {TextBackgroundColor::kLightBlue,
       {kBackgroundLightBlue, sizeof(kBackgroundLightBlue) - 1}},
      {TextBackgroundColor::kLightCyan,
       {kBackgroundLightCyan, sizeof(kBackgroundLightCyan) - 1}},
      {TextBackgroundColor::kLightGray,
       {kBackgroundLightGray, sizeof(kBackgroundLightGray) - 1}},
      {TextBackgroundColor::kLightGreen,
       {kBackgroundLightGreen, sizeof(kBackgroundLightGreen) - 1}},
      {TextBackgroundColor::kLightMagenta,
       {kBackgroundLightMagenta, sizeof(kBackgroundLightMagenta) - 1}},
      {TextBackgroundColor::kLightRed,
       {kBackgroundLightRed, sizeof(kBackgroundLightRed) - 1}},
      {TextBackgroundColor::kLightYellow,
       {kBackgroundLightYellow, sizeof(kBackgroundLightYellow) - 1}},
  };

  return background_color_map;
}

// Foreground color codes ------------------------------------------------------

const char kForegroundBlack[] = "\x1b[38;5;0m";
const char kForegroundBlue[] = "\x1b[38;5;4m";
const char kForegroundCyan[] = "\x1b[38;5;6m";
const char kForegroundGray[] = "\x1b[38;5;245m";
const char kForegroundGreen[] = "\x1b[38;5;2m";
const char kForegroundMagenta[] = "\x1b[38;5;5m";
const char kForegroundRed[] = "\x1b[38;5;1m";
const char kForegroundWhite[] = "\x1b[38;5;15m";
const char kForegroundYellow[] = "\x1b[38;5;11m";

const char kForegroundLightBlue[] = "\x1b[38;5;45m";
const char kForegroundLightCyan[] = "\x1b[38;5;87m";
const char kForegroundLightGray[] = "\x1b[38;5;250m";
const char kForegroundLightGreen[] = "\x1b[38;5;10m";
const char kForegroundLightMagenta[] = "\x1b[38;5;170m";
const char kForegroundLightRed[] = "\x1b[38;5;166m";
const char kForegroundLightYellow[] = "\x1b[38;5;190m";

using ForegroundColorMap =
    std::map<TextForegroundColor, std::pair<const char*, size_t>>;
static const ForegroundColorMap& GetForegroundColorMap() {
  // We substract 1 from the sizeof the strings to avoid the end null char.
  static ForegroundColorMap foreground_color_map = {
      {TextForegroundColor::kBlack,
       {kForegroundBlack, sizeof(kForegroundBlack) - 1}},
      {TextForegroundColor::kBlue,
       {kForegroundBlue, sizeof(kForegroundBlue) - 1}},
      {TextForegroundColor::kCyan,
       {kForegroundCyan, sizeof(kForegroundCyan) - 1}},
      {TextForegroundColor::kGray,
       {kForegroundGray, sizeof(kForegroundGray) - 1}},
      {TextForegroundColor::kGreen,
       {kForegroundGreen, sizeof(kForegroundGreen) - 1}},
      {TextForegroundColor::kMagenta,
       {kForegroundMagenta, sizeof(kForegroundMagenta) - 1}},
      {TextForegroundColor::kRed,
       {kForegroundRed, sizeof(kForegroundRed) - 1}},
      {TextForegroundColor::kWhite,
       {kForegroundWhite, sizeof(kForegroundWhite) - 1}},
      {TextForegroundColor::kYellow,
       {kForegroundYellow, sizeof(kForegroundYellow) - 1}},

      {TextForegroundColor::kLightBlue,
       {kForegroundLightBlue, sizeof(kForegroundLightBlue) - 1}},
      {TextForegroundColor::kLightCyan,
       {kForegroundLightCyan, sizeof(kForegroundLightCyan) - 1}},
      {TextForegroundColor::kLightGray,
       {kForegroundLightGray, sizeof(kForegroundLightGray) - 1}},
      {TextForegroundColor::kLightGreen,
       {kForegroundLightGreen, sizeof(kForegroundLightGreen) - 1}},
      {TextForegroundColor::kLightMagenta,
       {kForegroundLightMagenta, sizeof(kForegroundLightMagenta) - 1}},
      {TextForegroundColor::kLightRed,
       {kForegroundLightRed, sizeof(kForegroundLightRed) - 1}},
      {TextForegroundColor::kLightYellow,
       {kForegroundLightYellow, sizeof(kForegroundLightYellow) - 1}},
  };

  return foreground_color_map;
}

}  // namespace

OutputBuffer::Span::Span(Syntax s, std::string t) : syntax(s), text(t) {}

OutputBuffer::OutputBuffer() = default;
OutputBuffer::~OutputBuffer() = default;

// static
OutputBuffer OutputBuffer::WithContents(std::string str) {
  OutputBuffer result;
  result.Append(str);
  return result;
}

// static
OutputBuffer OutputBuffer::WithContents(Syntax syntax, std::string str) {
  OutputBuffer result;
  result.Append(syntax, str);
  return result;
}

void OutputBuffer::Append(std::string str) {
  spans_.push_back(Span(Syntax::kNormal, std::move(str)));
}

void OutputBuffer::Append(Syntax syntax, std::string str) {
  spans_.push_back(Span(syntax, std::move(str)));
}

void OutputBuffer::Append(OutputBuffer buf) {
  for (Span& span : buf.spans_)
    spans_.push_back(std::move(span));
}

void OutputBuffer::FormatHelp(const std::string& str) {
  for (fxl::StringView line :
       fxl::SplitString(str, "\n", fxl::kKeepWhitespace, fxl::kSplitWantAll)) {
    Syntax syntax;
    if (!line.empty() && line[0] != ' ') {
      // Nonempty lines beginning with non-whitespace are headings.
      syntax = Syntax::kHeading;
    } else {
      syntax = Syntax::kNormal;
    }

    spans_.push_back(Span(syntax, line.ToString()));
    spans_.push_back(Span(Syntax::kNormal, "\n"));
  }
}

void OutputBuffer::OutputErr(const Err& err) {
  spans_.push_back(Span(Syntax::kNormal, err.msg()));
}

void OutputBuffer::WriteToStdout() const {
  bool ended_in_newline = false;
  for (const Span& span : spans_) {
    // We apply syntax first. If normal, we see if any color are to be set.
    if (span.syntax != Syntax::kNormal) {
      auto syntax_pair = GetSyntaxColorMap().at(span.syntax);
      fwrite(syntax_pair.first, 1, syntax_pair.second, stdout);
    } else {
      if (span.background != TextBackgroundColor::kDefault) {
        auto color_pair = GetBackgroundColorMap().at(span.background);
        fwrite(color_pair.first, 1, color_pair.second, stdout);
      }
      if (span.foreground != TextForegroundColor::kDefault) {
        auto color_pair = GetForegroundColorMap().at(span.foreground);
        fwrite(color_pair.first, 1, color_pair.second, stdout);
      }
    }

    // The actual raw data to be outputted.
    fwrite(span.text.data(), 1, span.text.size(), stdout);

    // If any formatting was done, reset the attributes.
    if ((span.syntax != Syntax::kNormal) ||
        (span.background != TextBackgroundColor::kDefault) ||
        (span.foreground != TextForegroundColor::kDefault)) {
      fwrite(kNormalEscapeCode, 1, strlen(kNormalEscapeCode), stdout);
    }

    if (!span.text.empty())
      ended_in_newline = span.text.back() == '\n';
  }

  if (!ended_in_newline)
    fwrite("\n", 1, 1, stdout);
}

std::string OutputBuffer::AsString() const {
  std::string result;
  for (const Span& span : spans_)
    result.append(span.text);
  return result;
}

size_t OutputBuffer::UnicodeCharWidth() const {
  size_t result = 0;
  for (const Span& span : spans_)
    result += ::zxdb::UnicodeCharWidth(span.text);
  return result;
}

void OutputBuffer::SetBackgroundColor(TextBackgroundColor color) {
  for (Span& span : spans_)
    span.background = color;
}

void OutputBuffer::SetForegroundColor(TextForegroundColor color) {
  for (Span& span : spans_)
    span.foreground = color;
}

}  // namespace zxdb

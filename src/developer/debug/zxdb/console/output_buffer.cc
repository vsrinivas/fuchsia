// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/output_buffer.h"

#include <map>
#include <string_view>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/split_string.h"

namespace zxdb {

namespace {

// The color codes are taken from the vte 256 colorscheme, which is pretty
// common. If needed, some fallback colors could be established to support
// some old terminal scheme.

// Syntax color codes ----------------------------------------------------------

const char kNormalEscapeCode[] = "\x1b[0m";  // "[0m" = Normal.

using SyntaxColorMap = std::map<Syntax, std::string_view>;
static const SyntaxColorMap& GetSyntaxColorMap() {
  static SyntaxColorMap syntax_color_map = {
      {Syntax::kHeading, "\x1b[1m"},    // "[1m" = Bold.
      {Syntax::kComment, "\x1b[2m"},    // "[2m" = Faint.
      {Syntax::kError, "\x1b[31m"},     // "[31m" = Red.
      {Syntax::kWarning, "\x1b[33m"},   // "[33m" = Yellow.
      {Syntax::kSpecial, "\x1b[34m"},   // "[34m" = Blue.
      {Syntax::kReversed, "\x1b[7m"},   // "[7m" = Reverse video.
      {Syntax::kVariable, "\x1b[36m"},  // "[36m" = Cyan.
      {Syntax::kFileName, "\x1b[95m"},  // "[35m" = Bright Magenta.
  };

  return syntax_color_map;
}

// Background color codes ------------------------------------------------------

using BackgroundColorMap = std::map<TextBackgroundColor, std::string_view>;
static const BackgroundColorMap& GetBackgroundColorMap() {
  static BackgroundColorMap background_color_map = {
      {TextBackgroundColor::kBlack, "\x1b[48;5;0m"},
      {TextBackgroundColor::kBlue, "\x1b[48;5;4m"},
      {TextBackgroundColor::kCyan, "\x1b[48;5;6m"},
      {TextBackgroundColor::kGray, "\x1b[48;5;245m"},
      {TextBackgroundColor::kGreen, "\x1b[48;5;2m"},
      {TextBackgroundColor::kMagenta, "\x1b[48;5;5m"},
      {TextBackgroundColor::kRed, "\x1b[48;5;1m"},
      {TextBackgroundColor::kWhite, "\x1b[48;5;15m"},
      {TextBackgroundColor::kYellow, "\x1b[48;5;11m"},

      {TextBackgroundColor::kLightBlue, "\x1b[48;5;45m"},
      {TextBackgroundColor::kLightCyan, "\x1b[48;5;87m"},
      {TextBackgroundColor::kLightGray, "\x1b[48;5;250m"},
      {TextBackgroundColor::kLightGreen, "\x1b[48;5;10m"},
      {TextBackgroundColor::kLightMagenta, "\x1b[48;5;170m"},
      {TextBackgroundColor::kLightRed, "\x1b[48;5;166m"},
      {TextBackgroundColor::kLightYellow, "\x1b[48;5;190m"},
  };

  return background_color_map;
}

// Foreground color codes ------------------------------------------------------

using ForegroundColorMap = std::map<TextForegroundColor, std::string_view>;
static const ForegroundColorMap& GetForegroundColorMap() {
  // We subtract 1 from the sizeof the strings to avoid the end null char.
  static ForegroundColorMap foreground_color_map = {
      {TextForegroundColor::kBlack, "\x1b[38;5;0m"},
      {TextForegroundColor::kBlue, "\x1b[38;5;4m"},
      {TextForegroundColor::kCyan, "\x1b[38;5;6m"},
      {TextForegroundColor::kGray, "\x1b[38;5;245m"},
      {TextForegroundColor::kGreen, "\x1b[38;5;2m"},
      {TextForegroundColor::kMagenta, "\x1b[38;5;5m"},
      {TextForegroundColor::kRed, "\x1b[38;5;1m"},
      {TextForegroundColor::kWhite, "\x1b[38;5;15m"},
      {TextForegroundColor::kYellow, "\x1b[38;5;11m"},

      {TextForegroundColor::kLightBlue, "\x1b[38;5;45m"},
      {TextForegroundColor::kLightCyan, "\x1b[38;5;87m"},
      {TextForegroundColor::kLightGray, "\x1b[38;5;250m"},
      {TextForegroundColor::kLightGreen, "\x1b[38;5;10m"},
      {TextForegroundColor::kLightMagenta, "\x1b[38;5;170m"},
      {TextForegroundColor::kLightRed, "\x1b[38;5;166m"},
      {TextForegroundColor::kLightYellow, "\x1b[38;5;190m"},
  };

  return foreground_color_map;
}

// Writes the given string to stdout.
void FwriteStringView(std::string_view str) { fwrite(str.data(), 1, str.size(), stdout); }

}  // namespace

const char* SyntaxToString(Syntax syntax) {
  switch (syntax) {
    case Syntax::kNormal:
      return "kNormal";
    case Syntax::kComment:
      return "kComment";
    case Syntax::kHeading:
      return "kHeading";
    case Syntax::kError:
      return "kError";
    case Syntax::kWarning:
      return "kWarning";
    case Syntax::kSpecial:
      return "kSpecial";
    case Syntax::kReversed:
      return "kReversed";
    case Syntax::kVariable:
      return "kVariable";
    case Syntax::kFileName:
      return "kFileName";
  }
  return nullptr;
}

const char* TextBackgroundColorToString(TextBackgroundColor color) {
  switch (color) {
    case TextBackgroundColor::kDefault:
      return "kDefault";
    case TextBackgroundColor::kBlack:
      return "kBlack";
    case TextBackgroundColor::kBlue:
      return "kBlue";
    case TextBackgroundColor::kCyan:
      return "kCyan";
    case TextBackgroundColor::kGray:
      return "kGray";
    case TextBackgroundColor::kGreen:
      return "kGreen";
    case TextBackgroundColor::kMagenta:
      return "kMagenta";
    case TextBackgroundColor::kRed:
      return "kRed";
    case TextBackgroundColor::kYellow:
      return "kYellow";
    case TextBackgroundColor::kWhite:
      return "kWhite";
    case TextBackgroundColor::kLightBlue:
      return "kLightBlue";
    case TextBackgroundColor::kLightCyan:
      return "kLightCyan";
    case TextBackgroundColor::kLightGray:
      return "kLightGray";
    case TextBackgroundColor::kLightGreen:
      return "kLightGreen";
    case TextBackgroundColor::kLightMagenta:
      return "kLightMagenta";
    case TextBackgroundColor::kLightRed:
      return "kLightRed";
    case TextBackgroundColor::kLightYellow:
      return "kLightYellow";
  }
  return nullptr;
}

const char* TextForegroundColorToString(TextForegroundColor color) {
  switch (color) {
    case TextForegroundColor::kDefault:
      return "kDefault";
    case TextForegroundColor::kBlack:
      return "kBlack";
    case TextForegroundColor::kBlue:
      return "kBlue";
    case TextForegroundColor::kCyan:
      return "kCyan";
    case TextForegroundColor::kGray:
      return "kGray";
    case TextForegroundColor::kGreen:
      return "kGreen";
    case TextForegroundColor::kMagenta:
      return "kMagenta";
    case TextForegroundColor::kRed:
      return "kRed";
    case TextForegroundColor::kYellow:
      return "kYellow";
    case TextForegroundColor::kWhite:
      return "kWhite";
    case TextForegroundColor::kLightBlue:
      return "kLightBlue";
    case TextForegroundColor::kLightCyan:
      return "kLightCyan";
    case TextForegroundColor::kLightGray:
      return "kLightGray";
    case TextForegroundColor::kLightGreen:
      return "kLightGreen";
    case TextForegroundColor::kLightMagenta:
      return "kLightMagenta";
    case TextForegroundColor::kLightRed:
      return "kLightRed";
    case TextForegroundColor::kLightYellow:
      return "kLightYellow";
  }
  return nullptr;
}

OutputBuffer::Span::Span(Syntax s, std::string t) : syntax(s), text(std::move(t)) {}
OutputBuffer::Span::Span(std::string t, TextForegroundColor fg, TextBackgroundColor bg)
    : foreground(fg), background(bg), text(std::move(t)) {}

OutputBuffer::OutputBuffer() = default;

OutputBuffer::OutputBuffer(std::string str, TextForegroundColor fg, TextBackgroundColor bg) {
  spans_.emplace_back(std::move(str), fg, bg);
}

OutputBuffer::OutputBuffer(Syntax syntax, std::string str) {
  spans_.emplace_back(syntax, std::move(str));
}

OutputBuffer::~OutputBuffer() = default;

void OutputBuffer::Append(std::string str, TextForegroundColor fg, TextBackgroundColor bg) {
  spans_.emplace_back(std::move(str), fg, bg);
}

void OutputBuffer::Append(Syntax syntax, std::string str) {
  spans_.emplace_back(syntax, std::move(str));
}

void OutputBuffer::Append(OutputBuffer buf) {
  for (Span& span : buf.spans_)
    spans_.push_back(std::move(span));
}

void OutputBuffer::Append(const Err& err) { spans_.push_back(Span(Syntax::kNormal, err.msg())); }

void OutputBuffer::Append(Span span) { spans_.push_back(std::move(span)); }

void OutputBuffer::FormatHelp(const std::string& str) {
  for (auto line : fxl::SplitString(str, "\n", fxl::kKeepWhitespace, fxl::kSplitWantAll)) {
    Syntax syntax;
    if (!line.empty() && line[0] != ' ') {
      // Nonempty lines beginning with non-whitespace are headings.
      syntax = Syntax::kHeading;
    } else {
      syntax = Syntax::kNormal;
    }

    spans_.push_back(Span(syntax, std::string(line)));
    spans_.push_back(Span(Syntax::kNormal, "\n"));
  }
}

void OutputBuffer::WriteToStdout() const {
  bool ended_in_newline = false;
  for (const Span& span : spans_) {
    // We apply syntax first. If normal, we see if any color are to be set.
    if (span.syntax != Syntax::kNormal) {
      FwriteStringView(GetSyntaxColorMap().at(span.syntax));
    } else {
      if (span.background != TextBackgroundColor::kDefault)
        FwriteStringView(GetBackgroundColorMap().at(span.background));
      if (span.foreground != TextForegroundColor::kDefault)
        FwriteStringView(GetForegroundColorMap().at(span.foreground));
    }

    // The actual raw data to be outputted.
    FwriteStringView(span.text);

    // If any formatting was done, reset the attributes.
    if ((span.syntax != Syntax::kNormal) || (span.background != TextBackgroundColor::kDefault) ||
        (span.foreground != TextForegroundColor::kDefault))
      FwriteStringView(kNormalEscapeCode);

    if (!span.text.empty())
      ended_in_newline = span.text.back() == '\n';
  }

  if (!ended_in_newline)
    FwriteStringView("\n");
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

void OutputBuffer::TrimTrailingNewlines() {
  while (!spans_.empty()) {
    std::string& text = spans_.back().text;
    size_t last_good = text.find_last_not_of("\n");
    if (last_good != std::string::npos) {
      text.resize(last_good + 1);
      break;
    }

    spans_.pop_back();
  }
}

void OutputBuffer::Clear() { spans_.clear(); }

std::vector<OutputBuffer::Span> OutputBuffer::NormalizedSpans() const {
  std::vector<Span> normalized;
  for (const auto& cur : spans_) {
    if (normalized.empty()) {
      normalized.push_back(cur);
    } else {
      Span& prev = normalized.back();
      if (prev.syntax == cur.syntax && prev.background == cur.background &&
          prev.foreground == cur.foreground)
        prev.text.append(cur.text);  // Merge: continuation of same format.
      else
        normalized.push_back(cur);  // New format.
    }
  }

  return normalized;
}

std::string OutputBuffer::GetDebugString() const {
  // Normalize so the output is the same even if it was built with different
  // sequences of spans.
  std::vector<Span> normalized = NormalizedSpans();

  std::string result;
  for (size_t i = 0; i < normalized.size(); i++) {
    if (i > 0)
      result += ", ";

    result += SyntaxToString(normalized[i].syntax);
    if (normalized[i].background != TextBackgroundColor::kDefault ||
        normalized[i].foreground != TextForegroundColor::kDefault) {
      result += " ";
      result += TextBackgroundColorToString(normalized[i].background);
      result += " ";
      result += TextForegroundColorToString(normalized[i].foreground);
    }

    result += " \"";
    result += normalized[i].text;
    result.push_back('"');
  }
  return result;
}

bool OutputBuffer::operator==(const OutputBuffer& other) const {
  auto spans = NormalizedSpans();
  auto other_spans = other.NormalizedSpans();

  if (spans.size() != other_spans.size()) {
    return false;
  }

  for (size_t i = 0; i < spans.size(); i++) {
    auto& ours = spans[i];
    auto& theirs = other_spans[i];

    if (ours.syntax != theirs.syntax) {
      return false;
    }

    if (ours.syntax == Syntax::kNormal) {
      if (ours.foreground != theirs.foreground) {
        return false;
      }

      if (ours.background != theirs.background) {
        return false;
      }
    }

    if (ours.text != theirs.text) {
      return false;
    }
  }

  return true;
}

}  // namespace zxdb

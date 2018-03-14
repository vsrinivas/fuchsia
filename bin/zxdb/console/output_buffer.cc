// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/output_buffer.h"

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/public/lib/fxl/strings/split_string.h"

namespace zxdb {

namespace {

const char kNormalEscapeCode[] = "\x1b[0m";
const char kBoldEscapeCode[] = "\x1b[1m";

}  // namespace

OutputBuffer::Span::Span(Syntax s, std::string t) : syntax(s), text(t) {}

OutputBuffer::OutputBuffer() = default;
OutputBuffer::~OutputBuffer() = default;

void OutputBuffer::Append(std::string str) {
  spans_.push_back(Span(Syntax::kNormal, std::move(str)));
}

void OutputBuffer::Append(Syntax syntax, std::string str) {
  spans_.push_back(Span(syntax, std::move(str)));
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

void OutputBuffer::WriteToStdout() {
  bool ended_in_newline = false;
  for (const Span& span : spans_) {
    if (span.syntax == Syntax::kHeading)
      fwrite(kBoldEscapeCode, 1, strlen(kBoldEscapeCode), stdout);

    fwrite(span.text.data(), 1, span.text.size(), stdout);

    if (span.syntax != Syntax::kNormal)
      fwrite(kNormalEscapeCode, 1, strlen(kNormalEscapeCode), stdout);

    if (!span.text.empty())
      ended_in_newline = span.text.back() == '\n';
  }

  if (!ended_in_newline)
    fwrite("\n", 1, 1, stdout);
}

}  // namespace zxdb

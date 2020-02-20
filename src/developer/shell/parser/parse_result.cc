// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parse_result.h"

#include <deque>

namespace shell::parser {
namespace {

// Helper class to allow us to fork ParseResultStreams. Forking a stream means we get two streams,
// and each stream will yield the same results if we call Next() on it in order, *independent of*
// whether we call Next() on the other stream.
//
// Doing that means caching certain results when one side of the fork consumes them before the
// other, and this class holds the state involved in that.
class StreamFork {
 public:
  StreamFork(ParseResultStream stream) : stream_(std::move(stream)) {}

  // Handle a call to Next() for the A side of the stream.
  ParseResult ANext() {
    if (!end_ && a_results_.empty()) {
      Enqueue();
    }

    if (end_) {
      return *end_;
    }

    auto ret = a_results_.front();
    a_results_.pop_front();
    return ret;
  }

  // Handle a call to Next() for the B side of the stream.
  ParseResult BNext() {
    if (!end_ && b_results_.empty()) {
      Enqueue();
    }

    if (end_) {
      return *end_;
    }

    auto ret = b_results_.front();
    b_results_.pop_front();
    return ret;
  }

 private:
  // Poll the original stream and store its output in each of the two queues that feed our forked
  // stream.
  void Enqueue() {
    if (end_) {
      return;
    }

    auto result = stream_.Next();

    if (!result) {
      end_ = result;
    } else {
      a_results_.push_back(result);
      b_results_.push_back(result);
    }
  }

  ParseResultStream stream_;
  std::deque<ParseResult> a_results_;
  std::deque<ParseResult> b_results_;
  std::optional<ParseResult> end_ = std::nullopt;
};

}  // namespace

std::pair<ParseResultStream, ParseResultStream> ParseResultStream::Fork() && {
  bool ok = this->ok();
  auto fork = std::make_shared<StreamFork>(std::move(*this));

  auto a = ParseResultStream(ok, [fork]() mutable { return fork->ANext(); });
  auto b = ParseResultStream(ok, [fork]() mutable { return fork->BNext(); });

  return {std::move(a), std::move(b)};
}

ParseResultStream ParseResultStream::Fail() && {
  ok_ = false;
  return std::move(*this);
}

ParseResultStream ParseResultStream::Follow(fit::function<ParseResultStream(ParseResult)> next) && {
  // Takes the least-erroneous result from this stream and uses it as the prefix for the given
  // parser. In the future lots of interesting error handling stuff will happen here (backtracking!)
  // but for now this will do.
  return next(Next());
}

ParseResultStream ParseResultStream::Map(fit::function<ParseResult(ParseResult)> mapper) && {
  auto ok = ok_;
  return ParseResultStream(ok, [old = std::move(*this), mapper = std::move(mapper)]() mutable {
    return mapper(old.Next());
  });
}

}  // namespace shell::parser

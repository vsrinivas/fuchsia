// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/redact/replacer.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <utility>

#include <re2/re2.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {

Replacer ReplaceWithText(const std::string_view pattern, const std::string_view replacement) {
  auto regexp = std::make_unique<re2::RE2>(pattern);
  if (!regexp->ok()) {
    FX_LOGS(ERROR) << "Failed to compile regexp: \"" << pattern << "\"";
    return nullptr;
  }

  return [regexp = std::move(regexp), replace = std::string(replacement)](
             RedactionIdCache& cache, std::string& text) -> std::string& {
    RE2::GlobalReplace(&text, *regexp, replace);
    return text;
  };
}

namespace {

// Replaces all non-overlapping instances of the keys in |redactions| with their values.
//
// For example, replacing "bc" with "1" and "c" with "2" in "abc" will result in "a1".
void ApplyRedactions(const std::map<std::string, std::string> redactions, std::string& text) {
  // Grouping of a string and its position in |text|.
  using Substr = std::pair<size_t, const std::string*>;
  auto Compare = [](const Substr& lhs, const Substr& rhs) { return rhs.first < lhs.first; };
  auto Overlap = [](const Substr& early, const Substr& later) {
    return early.first + early.second->size() > later.first;
  };

  // Keep the next substring to replace at the front of |queue|.
  std::priority_queue<Substr, std::vector<Substr>, decltype(Compare)> queue(std::move(Compare));

  // Seed |queue| with the position of the first instance of each key in |redactions|.
  for (const auto& [original, _] : redactions) {
    const size_t pos = text.find(original);
    if (pos != text.npos) {
      queue.push({pos, &original});
    }
  }

  std::vector<Substr> to_replace;
  while (!queue.empty()) {
    Substr top = queue.top();
    queue.pop();

    const size_t pos = top.first;
    const std::string& original = *(top.second);

    // Add the next instance of |original| to |queue|, if one exists.
    if (const size_t next_pos = text.find(original, pos + original.size()); next_pos != text.npos) {
      queue.push({next_pos, &original});
    }

    // Only add non-overlapping strings to |to_replace|.
    if (to_replace.empty() || !Overlap(to_replace.back(), top)) {
      to_replace.push_back(std::move(top));
    }
  }

  // Replace each substring in |to_replace|.
  int adjustment{0};
  for (const auto& [pos, original] : to_replace) {
    const auto& redacted = redactions.at(*original);
    text.replace(pos + adjustment, original->size(), redacted);

    // Account for the size difference between original and replacement text when replacing later
    // instances.
    adjustment += redacted.size();
    adjustment -= original->size();
  }
}

// Finds strings in |text| that match |regexp| and constructs their redacted replacements with
// |build_redacted|.
std::map<std::string, std::string> BuildRedactions(
    const std::string& text, const re2::RE2& regexp,
    ::fit::function<std::string(const std::string& match)> build_redacted) {
  std::map<std::string, std::string> redactions;

  re2::StringPiece text_view(text);
  std::string match;
  while (RE2::FindAndConsume(&text_view, regexp, &match)) {
    if (!match.empty()) {
      redactions[match] = build_redacted(match);
    } else {
      FX_LOGS(INFO) << "EMPTY MATCH";
    }
  }

  return redactions;
}

// Builds a Replacer that redacts instances of |pattern| with strings constructed by
// |build_redacted|.
//
// Returns nullptr if pattern produces a bad regexp.
Replacer FunctionBasedReplacer(
    const std::string_view pattern,
    ::fit::function<std::string(RedactionIdCache& cache, const std::string& match)>
        build_redacted) {
  auto regexp = std::make_unique<re2::RE2>(pattern);
  if (!regexp->ok()) {
    FX_LOGS(ERROR) << "Failed to compile regexp: \"" << pattern << "\"";
    return nullptr;
  }

  if (regexp->NumberOfCapturingGroups() != 1) {
    FX_LOGS(ERROR) << "Regexp \"" << pattern << "\" expected to have 1 capture group, has "
                   << regexp->NumberOfCapturingGroups();
    return nullptr;
  }

  return [regexp = std::move(regexp), build_redacted = std::move(build_redacted)](
             RedactionIdCache& cache, std::string& text) mutable -> std::string& {
    const auto redactions =
        BuildRedactions(text, *regexp, [&cache, &build_redacted](const std::string& match) {
          return build_redacted(cache, match);
        });
    ApplyRedactions(redactions, text);
    return text;
  };
}

}  // namespace

Replacer ReplaceWithIdFormatString(const std::string_view pattern,
                                   const std::string_view format_str) {
  bool specificier_found{false};

  for (size_t pos{0}; (pos = format_str.find("%d", pos)) != format_str.npos; ++pos) {
    if (specificier_found) {
      FX_LOGS(ERROR) << "Format string \"" << format_str
                     << "\" expected to have 1 \"%d\" specifier";
      return nullptr;
    }
    specificier_found = true;
  }

  if (!specificier_found) {
    FX_LOGS(ERROR) << "Format string \"" << format_str << "\" expected to have 1 \"%d\" specifier";
    return nullptr;
  }

  return FunctionBasedReplacer(pattern, [format = std::string(format_str)](
                                            RedactionIdCache& cache, const std::string& match) {
    return fxl::StringPrintf(format.c_str(), cache.GetId(match));
  });
}

namespace {

constexpr std::string_view kIPv4Pattern{R"(\b()"
                                        R"((?:(?:25[0-5]|(?:2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3})"
                                        R"((?:25[0-5]|(?:2[0-4]|1{0,1}[0-9]){0,1}[0-9]))"
                                        R"()\b)"};

// 0.*.*.* = current network (as source)
// 127.*.*.* = loopback
// 169.254.*.* = link-local addresses
// 224.0.0.* = link-local multicast
static re2::RE2 kCleartextIPv4{R"(^0\..*)"
                               R"(|)"
                               R"(^127\..*)"
                               R"(|)"
                               R"(^169\.254\..*)"
                               R"(|)"
                               R"(^224\.0\.0\..*)"
                               R"(|)"
                               R"(^255.255.255.255$)"};

std::string RedactIPv4(RedactionIdCache& cache, const std::string& match) {
  return re2::RE2::FullMatch(match, kCleartextIPv4)
             ? match
             : fxl::StringPrintf("<REDACTED-IPV4: %d>", cache.GetId(match));
}

}  // namespace

Replacer ReplaceIPv4() { return FunctionBasedReplacer(kIPv4Pattern, RedactIPv4); }

namespace {

constexpr std::string_view kIPv6Pattern{
    // IPv6 without ::
    R"(()"
    R"(\b(?:(?:[[:xdigit:]]{1,4}:){7}[[:xdigit:]]{1,4})\b)"
    R"(|)"
    //  IPv6 with embedded ::
    R"(\b(?:(?:[[:xdigit:]]{1,4}:)+:(?:[[:xdigit:]]{1,4}:)*[[:xdigit:]]{1,4})\b)"
    R"(|)"
    // IPv6 starting with :: and 3-7 non-zero fields
    R"(::[[:xdigit:]]{1,4}(?::[[:xdigit:]]{1,4}){2,6}\b)"
    R"(|)"
    // IPv6 with 3-7 non-zero fields ending with ::
    R"(\b[[:xdigit:]]{1,4}(?::[[:xdigit:]]{1,4}){2,6}::)"
    R"())"};

// ff.1:** and ff.2:** = local multicast
static re2::RE2 kCleartextIPv6{R"((?i)^ff[[:xdigit:]][12]:)"};

// ff..:** = multicast - display first 2 bytes and redact
static re2::RE2 kMulticastIPv6{R"((?i)^(ff[[:xdigit:]][[:xdigit:]]:))"};

// fe80/10 = link-local - display first 2 bytes and redact
static re2::RE2 kLinkLocalIPv6{R"((?i)^(fe[89ab][[:xdigit:]]:))"};

// ::ffff:*:* = IPv4
static re2::RE2 kIPv4InIPv6{R"((?i)^::f{4}(:[[:xdigit:]]{1,4}){2}$)"};

std::string RedactIPv6(RedactionIdCache& cache, const std::string& match) {
  if (re2::RE2::PartialMatch(match, kCleartextIPv6)) {
    return match;
  }

  const int id = cache.GetId(match);

  std::string submatch;
  if (re2::RE2::PartialMatch(match, kMulticastIPv6, &submatch)) {
    return fxl::StringPrintf("%s<REDACTED-IPV6-MULTI: %d>", submatch.c_str(), id);
  }

  if (re2::RE2::PartialMatch(match, kLinkLocalIPv6, &submatch)) {
    return fxl::StringPrintf("%s<REDACTED-IPV6-LL: %d>", submatch.c_str(), id);
  }

  if (re2::RE2::FullMatch(match, kIPv4InIPv6)) {
    return fxl::StringPrintf("::ffff:<REDACTED-IPV4: %d>", id);
  }

  return fxl::StringPrintf("<REDACTED-IPV6: %d>", id);
}

}  // namespace

Replacer ReplaceIPv6() { return FunctionBasedReplacer(kIPv6Pattern, RedactIPv6); }

namespace {

constexpr std::string_view kMacPattern{
    R"(\b()"
    R"(\b(?:(?:[0-9a-fA-F]{1,2}(?:[\.:-])){3})(?:[0-9a-fA-F]{1,2}(?:[\.:-])){2}[0-9a-fA-F]{1,2}\b)"
    R"()\b)"};

static re2::RE2 kOui{R"(^((?:[0-9a-fA-F]{1,2}(?:[\.:-])){3}))"};

std::string RedactMac(RedactionIdCache& cache, const std::string& match) {
  const int id = cache.GetId(match);

  std::string oui;
  if (!re2::RE2::PartialMatch(match, kOui, &oui)) {
    oui = "regex error";
  }

  return fxl::StringPrintf("%s<REDACTED-MAC: %d>", oui.c_str(), id);
}

}  // namespace

Replacer ReplaceMac() { return FunctionBasedReplacer(kMacPattern, RedactMac); }

}  // namespace forensics

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/redact/redactor.h"

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/syslog/cpp/macros.h>

#include <string_view>

namespace forensics {
namespace {

// Email stub alice@website.tld
constexpr std::string_view kEmailPattern = R"([a-zA-Z0-9]*@[a-zA-Z0-9]*\.[a-zA-Z]*)";

// uuid
constexpr std::string_view kUuidPattern =
    R"([0-9a-fA-F]{8}\b-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-\b[0-9a-fA-F]{12})";

// http(s) urls
constexpr std::string_view kUrlPattern = R"(https?://[^"',;!<> ]*)";

// The SSID identifier contains at most 32 pairs of hexadecimal characters, but match any number so
// SSID identifiers with the wrong number of hexadecimal characters are also redacted.
constexpr std::string_view kSsidPattern = R"((<ssid-[0-9a-fA-F]*>))";

// Long hex strings
constexpr std::string_view kHexPattern = R"((\b[0-9a-fA-F]{32}\b))";

// Obfuscated gaia ids
constexpr std::string_view kGaiaPattern = R"((\b1[0-9]{20}\b))";

constexpr std::string_view kUnredactedCanary =
    R"(Email: alice@website.tld,
IPv4: 8.8.8.8,
IPv4_New: 8.9.10.42,
IPv4_Dup: 8.8.8.8,
IPv4_WithPort: 8.8.8.8:8080,
IPv461: ::ffff:12.34.56.78,
IPv462: ::ffff:ab12:cd34,
IPv6: 2001:503:eEa3:0:0:0:0:30,
IPv6_WithPort: [2001:503:eEa3:0:0:0:0:30]:8080,
IPv6C: fec8::7d84:c1dc:ab34:656a,
IPv6LL: fe80::7d84:c1dc:ab34:656a,
UUID: ddd0fA34-1016-11eb-adc1-0242ac120002,
MAC: de:ad:BE:EF:42:5a,
SSID: <ssid-666F6F>,
HTTP: http://fuchsia.dev/fuchsia/testing?q=Test,
HTTPS: https://fuchsia.dev/fuchsia/testing?q=Test,
HEX: 1234567890abcdefABCDEF0123456789,
v4Current: 0.1.2.3,
v4Loopback: 127.1.2.3,
v4LocalAddr: 169.254.12.34,
v4LocalMulti: 224.0.0.123,
v4Multi: 224.0.1.123,
broadcast: 255.255.255.255,
v6zeroes: :: ::1,
v6LeadingZeroes: ::abcd:dcba:bcde:f,
v6TrailingZeroes: f:e:d:c:abcd:dcba:bcde::,
v6LinkLocal: feB2:111:222:333:444:555:666:777,
v6LocalMulticast: ff72:111:222:333:444:555:666:777,
v6Multicast: ff77:111:222:333:444:555:666:777,
obfuscatedGaiaId: 106986199446298680449)";

constexpr std::string_view kRedactedCanary =
    R"(Email: <REDACTED-EMAIL>,
IPv4: <REDACTED-IPV4: 1>,
IPv4_New: <REDACTED-IPV4: 2>,
IPv4_Dup: <REDACTED-IPV4: 1>,
IPv4_WithPort: <REDACTED-IPV4: 1>:8080,
IPv461: ::ffff:<REDACTED-IPV4: 3>,
IPv462: ::ffff:<REDACTED-IPV4: 5>,
IPv6: <REDACTED-IPV6: 6>,
IPv6_WithPort: [<REDACTED-IPV6: 6>]:8080,
IPv6C: <REDACTED-IPV6: 7>,
IPv6LL: fe80:<REDACTED-IPV6-LL: 8>,
UUID: <REDACTED-UUID>,
MAC: de:ad:BE:<REDACTED-MAC: 13>,
SSID: <REDACTED-SSID: 14>,
HTTP: <REDACTED-URL>,
HTTPS: <REDACTED-URL>,
HEX: <REDACTED-HEX: 15>,
v4Current: 0.1.2.3,
v4Loopback: 127.1.2.3,
v4LocalAddr: 169.254.12.34,
v4LocalMulti: 224.0.0.123,
v4Multi: <REDACTED-IPV4: 4>,
broadcast: 255.255.255.255,
v6zeroes: :: ::1,
v6LeadingZeroes: <REDACTED-IPV6: 9>,
v6TrailingZeroes: <REDACTED-IPV6: 10>,
v6LinkLocal: feB2:<REDACTED-IPV6-LL: 11>,
v6LocalMulticast: ff72:111:222:333:444:555:666:777,
v6Multicast: ff77:<REDACTED-IPV6-MULTI: 12>,
obfuscatedGaiaId: <REDACTED-OBFUSCATED-GAIA-ID: 16>)";

}  // namespace

RedactorBase::RedactorBase(inspect::BoolProperty redaction_enabled)
    : redaction_enabled_(std::move(redaction_enabled)) {}

Redactor::Redactor(const int starting_id, inspect::UintProperty cache_size,
                   inspect::BoolProperty redaction_enabled)
    : RedactorBase(std::move(redaction_enabled)), cache_(std::move(cache_size), starting_id) {
  Add(ReplaceIPv4())
      .Add(ReplaceIPv6())
      .Add(ReplaceMac())
      .AddTextReplacer(kEmailPattern, "<REDACTED-EMAIL>")
      .AddTextReplacer(kUuidPattern, "<REDACTED-UUID>")
      .AddTextReplacer(kUrlPattern, "<REDACTED-URL>")
      .AddIdReplacer(kSsidPattern, "<REDACTED-SSID: %d>")
      .AddIdReplacer(kHexPattern, "<REDACTED-HEX: %d>")
      .AddIdReplacer(kGaiaPattern, "<REDACTED-OBFUSCATED-GAIA-ID: %d>");
}

std::string& Redactor::Redact(std::string& text) {
  for (const auto& replacer : replacers_) {
    replacer(cache_, text);
  }
  return text;
}

Redactor& Redactor::Add(Replacer replacer) {
  FX_CHECK(replacer != nullptr);
  replacers_.push_back(std::move(replacer));
  return *this;
}

Redactor& Redactor::AddTextReplacer(std::string_view pattern, std::string_view replacement) {
  auto replacer = ReplaceWithText(pattern, replacement);
  FX_CHECK(replacer != nullptr) << "Failed to build replacer for " << pattern << " " << replacement;

  return Add(std::move(replacer));
}

Redactor& Redactor::AddIdReplacer(std::string_view pattern, std::string_view format) {
  auto replacer = ReplaceWithIdFormatString(pattern, format);
  FX_CHECK(replacer != nullptr) << "Failed to build replacer for " << pattern << " " << format;

  return Add(std::move(replacer));
}

std::string Redactor::UnredactedCanary() const { return std::string(kUnredactedCanary); }

std::string Redactor::RedactedCanary() const { return std::string(kRedactedCanary); }

IdentityRedactor::IdentityRedactor(inspect::BoolProperty redaction_enabled)
    : RedactorBase(std::move(redaction_enabled)) {}

std::string& IdentityRedactor::Redact(std::string& text) { return text; }

std::string IdentityRedactor::UnredactedCanary() const { return std::string(kUnredactedCanary); }

std::string IdentityRedactor::RedactedCanary() const { return std::string(kUnredactedCanary); }

}  // namespace forensics

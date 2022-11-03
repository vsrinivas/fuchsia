// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

#include <lib/syslog/cpp/macros.h>

namespace mdns {

namespace {

static const std::string kLocalDomainName = "local.";
static const std::string kSubtypeSeparator = "._sub.";
static const std::string kLabelSeparator = ".";
static const std::string kTcpSuffix = "._tcp.";
static const std::string kUdpSuffix = "._udp.";

static constexpr size_t kMaxHostNameLength = 253 - 6;  // 6 for local domain.
static constexpr size_t kMaxServiceFirstLabelLength = 16;
static constexpr size_t kMaxTextStringLength = 255;
static constexpr size_t kMaxLabelLength = 63;

// Concatenates |strings|.
std::string Concatenate(const std::initializer_list<const std::string*>& strings) {
  std::string result;
  size_t result_size = 0;

  for (auto string : strings) {
    FX_DCHECK(string);
    result_size += string->length();
  }

  result.reserve(result_size);

  for (const auto& string : strings) {
    FX_DCHECK(string);
    result.append(*string);
  }

  return result;
}

// Parses a string. Match functions either return true and update the position
// of the parser or return false and leave the position unchanged.
class Parser {
 public:
  Parser(const std::string& str) : str_(str), pos_(0) {}

  // Matches end-of-string.
  bool MatchEnd() { return pos_ == str_.length(); }

  // Matches a specified string.
  bool Match(const std::string& to_match) {
    if (pos_ + to_match.length() > str_.length()) {
      return false;
    }

    if (str_.compare(pos_, to_match.length(), to_match) != 0) {
      return false;
    }

    pos_ += to_match.length();

    return true;
  }

  // Matches a DNS label, which must be at the start of string or be preceded by
  // a '.'.
  bool MatchDnsLabel(std::string* label_out = nullptr) {
    if (pos_ == str_.length()) {
      return false;
    }

    size_t new_pos = str_.find(kLabelSeparator, pos_);
    if (new_pos == pos_) {
      // Zero length.
      return false;
    } else if (new_pos == std::string::npos) {
      new_pos = str_.length();
    }

    if (new_pos - pos_ > kMaxLabelLength) {
      // Too long.
      return false;
    }

    if (label_out) {
      *label_out = str_.substr(pos_, new_pos - pos_);
    }

    pos_ = new_pos;

    return true;
  }

  // Matches a service name, including the trailing '_tcp.' or '_udp.'.
  bool MatchServiceName(std::string* service_name_out = nullptr) {
    size_t initial_pos = pos_;

    if (MatchDnsLabel() && str_[initial_pos] == '_' &&
        pos_ - initial_pos <= kMaxServiceFirstLabelLength &&
        (Match(kTcpSuffix) || Match(kUdpSuffix))) {
      if (service_name_out) {
        *service_name_out = str_.substr(initial_pos, pos_ - initial_pos);
      }

      return true;
    }

    pos_ = initial_pos;
    return false;
  }

  // Resets the position to the start of the string.
  void Restart() { pos_ = 0; }

 private:
  const std::string& str_;
  size_t pos_;
};

}  // namespace

// static
const std::string MdnsNames::kAnyServiceFullName = "_services._dns-sd._udp.local.";

// static
std::string MdnsNames::HostFullName(const std::string& host_name) {
  FX_DCHECK(IsValidHostName(host_name));

  return Concatenate({&host_name, &kLabelSeparator, &kLocalDomainName});
}

// static
std::string MdnsNames::HostNameFromFullName(const std::string& host_full_name) {
  FX_DCHECK(host_full_name.size() > kLocalDomainName.size());

  return host_full_name.substr(0, host_full_name.size() - kLocalDomainName.size() - 1);
}

// static
std::string MdnsNames::ServiceFullName(const std::string& service_name) {
  FX_DCHECK(IsValidServiceName(service_name));

  return Concatenate({&service_name, &kLocalDomainName});
}

// static
std::string MdnsNames::ServiceSubtypeFullName(const std::string& service_name,
                                              const std::string& subtype) {
  FX_DCHECK(IsValidServiceName(service_name));
  FX_DCHECK(IsValidSubtypeName(subtype));

  return Concatenate({&subtype, &kSubtypeSeparator, &service_name, &kLocalDomainName});
}

// static
std::string MdnsNames::InstanceFullName(const std::string& instance_name,
                                        const std::string& service_name) {
  FX_DCHECK(IsValidInstanceName(instance_name));
  FX_DCHECK(IsValidServiceName(service_name));

  return Concatenate({&instance_name, &kLabelSeparator, &service_name, &kLocalDomainName});
}

// static
bool MdnsNames::SplitInstanceFullName(const std::string& instance_full_name,
                                      std::string* instance_name_out,
                                      std::string* service_name_out) {
  FX_DCHECK(instance_name_out);
  FX_DCHECK(service_name_out);

  // instance_name "." service_name kLocalDomainName

  Parser parser(instance_full_name);
  return parser.MatchDnsLabel(instance_name_out) && parser.Match(kLabelSeparator) &&
         parser.MatchServiceName(service_name_out) && parser.Match(kLocalDomainName) &&
         parser.MatchEnd();
}

// static
bool MdnsNames::MatchServiceName(const std::string& name, const std::string& service_name,
                                 std::string* subtype_out) {
  FX_DCHECK(IsValidServiceName(service_name));
  FX_DCHECK(subtype_out);

  // [ subtype kSubtypeSeparator ] service_name kLocalDomainName

  Parser parser(name);
  if (!parser.MatchDnsLabel(subtype_out) || !parser.Match(kSubtypeSeparator)) {
    *subtype_out = "";
    parser.Restart();
  }

  return parser.Match(service_name) && parser.Match(kLocalDomainName) && parser.MatchEnd();
}

// static
bool MdnsNames::IsValidHostName(const std::string& host_name) {
  // A host name is one or more labels separated by '.'s. A label is 1..63
  // characters long not including separators. A complete host name with
  // separators must be at most 247 characters long (253 minus 6 to
  // accommodate a ".local" suffix).
  if (host_name.length() > kMaxHostNameLength) {
    return false;
  }

  Parser parser(host_name);
  if (!parser.MatchDnsLabel()) {
    return false;
  }

  while (!parser.MatchEnd()) {
    if (!parser.Match(kLabelSeparator) || !parser.MatchDnsLabel()) {
      return false;
    }
  }

  return true;
}

// static
bool MdnsNames::IsValidServiceName(const std::string& service_name) {
  // A service name is two labels, both terminated with '.'. The first label
  // must be [1..16] characters, and the first character must be '_'. The
  // second label must be "_tcp" or "_udp".
  Parser parser(service_name);
  return parser.MatchServiceName() && parser.MatchEnd();
}

// static
bool MdnsNames::IsValidInstanceName(const std::string& instance_name) {
  // Instance names consist of a single label.
  return instance_name.length() > 0 && instance_name.length() <= kMaxLabelLength &&
         instance_name.find(kLabelSeparator) == std::string::npos;
}

// static
bool MdnsNames::IsValidSubtypeName(const std::string& subtype_name) {
  // Subtype names consist of a single label.
  return subtype_name.length() > 0 && subtype_name.length() <= kMaxLabelLength &&
         subtype_name.find(kLabelSeparator) == std::string::npos;
}

// static
bool MdnsNames::IsValidTextString(const std::string& text_string) {
  // Text strings must be at most 255 characters long.
  return text_string.length() <= kMaxTextStringLength;
}

// static
bool MdnsNames::IsValidTextString(const std::vector<uint8_t>& text_string) {
  // Text strings must be at most 255 characters long.
  return text_string.size() <= kMaxTextStringLength;
}

// static
std::string MdnsNames::AltHostName(const std::string& host_name) {
  static constexpr size_t kExpectedUnmodifiedHostNameSize = 22;
  static constexpr size_t kBlock0Pos = 8;
  static constexpr size_t kBlock1Pos = 13;
  static constexpr size_t kBlock2Pos = 18;
  static constexpr size_t kBlockSize = 4;

  if (host_name.size() != kExpectedUnmodifiedHostNameSize) {
    return host_name;
  }

  // "fuchsia-1234-5678-9abc" becomes "12345678ABC".
  std::string result;
  result.reserve(kBlockSize * 3);
  result.append(host_name.substr(kBlock0Pos, kBlockSize));
  result.append(host_name.substr(kBlock1Pos, kBlockSize));
  result.append(host_name.substr(kBlock2Pos, kBlockSize));
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);

  return result;
}

}  // namespace mdns

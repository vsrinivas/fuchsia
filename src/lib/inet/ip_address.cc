// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inet/ip_address.h"

#include <arpa/inet.h>
#include <endian.h>
#include <sys/socket.h>

#include <sstream>

namespace inet {
namespace {

constexpr size_t kV6WordCount = 8;
constexpr uint8_t kV4LinkLocalFirstByte = 169;
constexpr uint8_t kV4LinkLocalSecondByte = 254;

// Parses a string. Match functions either return true and update the position of the parser or
// return false and leave the position unchanged. If a Match function has an out-parameter, the
// actual out-parameter is only modified if the function returns true.
class Parser {
 public:
  Parser(const std::string_view& str) : str_(str), pos_(0) {}

  size_t position() const { return pos_; }

  // Matches end-of-string.
  bool MatchEnd() { return pos_ == str_.length(); }

  // Matches a specified character.
  bool Match(char to_match) {
    if (pos_ == str_.length() || str_[pos_] != to_match) {
      return false;
    }

    ++pos_;

    return true;
  }

  // Matches a single decimal digit.
  bool MatchDecDigit(uint8_t* out) {
    FX_DCHECK(out);

    if (pos_ == str_.length() || !std::isdigit(static_cast<unsigned char>(str_[pos_]))) {
      return false;
    }

    *out = str_[pos_] - '0';
    ++pos_;

    return true;
  }

  // Matches a single hexadecimal digit.
  bool MatchHexDigit(uint8_t* out) {
    FX_DCHECK(out);

    if (pos_ == str_.length() || !std::isxdigit(static_cast<unsigned char>(str_[pos_]))) {
      return false;
    }

    if (std::isdigit(static_cast<unsigned char>(str_[pos_]))) {
      *out = str_[pos_] - '0';
    } else if (std::islower(static_cast<unsigned char>(str_[pos_]))) {
      *out = 10 + (str_[pos_] - 'a');
    } else {
      *out = 10 + (str_[pos_] - 'A');
    }

    ++pos_;

    return true;
  }

  // Matches a decimal byte of at most 3 digits. The match will succeed even if the decimal byte is
  // followed immediately by a digit. If matching three digits would produce a value greater than
  // 255, only two digits are matched.
  bool MatchMax3DigitDecByte(uint8_t* byte_out) {
    FX_DCHECK(byte_out);

    uint8_t digit = 0;
    if (!MatchDecDigit(&digit)) {
      return false;
    }

    uint16_t accum = digit;
    if (MatchDecDigit(&digit)) {
      accum = accum * 10 + digit;
      if (accum <= 25 && MatchDecDigit(&digit)) {
        if (accum < 25 || digit < 6) {
          accum = accum * 10 + digit;
        } else {
          // Including that last digit would produce a value > 255.
          --pos_;
        }
      }
    }

    FX_DCHECK(accum <= 255);
    *byte_out = static_cast<uint8_t>(accum);
    return true;
  }

  // Matches a hexadecimal word of at most 4 digits. The match will succeed even if the hexadecimal
  // word is followed immediately by a hexadecimal digit.
  bool MatchMax4DigitHexWord(uint16_t* word_out) {
    FX_DCHECK(word_out);

    uint8_t digit = 0;
    if (!MatchHexDigit(&digit)) {
      return false;
    }

    uint16_t accum = digit;
    if (MatchHexDigit(&digit)) {
      accum = accum * 16 + digit;
      if (MatchHexDigit(&digit)) {
        accum = accum * 16 + digit;
        if (MatchHexDigit(&digit)) {
          accum = accum * 16 + digit;
        }
      }
    }

    *word_out = accum;
    return true;
  }

  // Matches 1..max hexadecimal words of at most 4 digits separated by colons.
  bool MatchMax4DigitHexWordList(size_t max, std::vector<uint16_t>* words_out) {
    FX_DCHECK(words_out);

    std::vector<uint16_t> words;
    uint16_t word;
    if (!MatchMax4DigitHexWord(&word)) {
      return false;
    }

    words.push_back(word);

    while (words.size() < max) {
      size_t old_pos = pos_;
      if (!Match(':') || !MatchMax4DigitHexWord(&word)) {
        pos_ = old_pos;
        break;
      }

      words.push_back(word);
    }

    *words_out = std::move(words);
    return true;
  }

  // Matches an IPV4 address.
  bool MatchIpV4Address(IpAddress* address_out) {
    FX_DCHECK(address_out);

    size_t old_pos = pos_;
    uint8_t b0, b1, b2, b3;
    if (MatchMax3DigitDecByte(&b0) && Match('.') && MatchMax3DigitDecByte(&b1) && Match('.') &&
        MatchMax3DigitDecByte(&b2) && Match('.') && MatchMax3DigitDecByte(&b3)) {
      *address_out = IpAddress(b0, b1, b2, b3);
      return true;
    }

    pos_ = old_pos;
    return false;
  }

  // Matches an IPV6 address.
  bool MatchIpV6Address(IpAddress* address_out) {
    FX_DCHECK(address_out);

    size_t old_pos = pos_;

    std::vector<uint16_t> words;
    if (MatchMax4DigitHexWordList(kV6WordCount, &words)) {
      if (words.size() == kV6WordCount) {
        // List of 8 words.
        *address_out = IpAddress(words);
        return true;
      }
    }

    if (Match(':') && Match(':')) {
      if (words.size() == kV6WordCount - 1) {
        // 7 words followed by a pair of colons.
        *address_out = IpAddress(words);
        return true;
      }

      std::vector<uint16_t> more_words;
      if (MatchMax4DigitHexWordList(kV6WordCount - 1 - words.size(), &more_words)) {
        while (words.size() + more_words.size() < kV6WordCount) {
          words.push_back(0);
        }

        for (const auto word : more_words) {
          words.push_back(word);
        }

        FX_CHECK(words.size() == kV6WordCount);
      }

      *address_out = IpAddress(words);
      return true;
    }

    // Fail.
    pos_ = old_pos;
    return false;
  }

 private:
  const std::string_view& str_;
  size_t pos_;
};

}  // namespace

// static
const IpAddress IpAddress::kInvalid;
// static
const IpAddress IpAddress::kV4Loopback(127, 0, 0, 1);
// static
const IpAddress IpAddress::kV6Loopback(0, 0, 0, 0, 0, 0, 0, 1);

// static
IpAddress IpAddress::FromString(const std::string& address_string, sa_family_t family) {
  FX_DCHECK(family == AF_UNSPEC || family == AF_INET || family == AF_INET6);

  std::string_view address_string_view(address_string);
  Parser parser(address_string_view);
  IpAddress address;

  if (((family != AF_INET6 && parser.MatchIpV4Address(&address)) ||
       (family != AF_INET && parser.MatchIpV6Address(&address))) &&
      parser.MatchEnd()) {
    return address;
  }

  return kInvalid;
}

// static
std::pair<IpAddress, size_t> IpAddress::FromStringView(const std::string_view string_view,
                                                       sa_family_t family) {
  FX_DCHECK(family == AF_UNSPEC || family == AF_INET || family == AF_INET6);

  Parser parser(string_view);
  IpAddress address;
  if ((family != AF_INET6 && parser.MatchIpV4Address(&address)) ||
      (family != AF_INET && parser.MatchIpV6Address(&address))) {
    return std::make_pair(address, parser.position());
  }

  return std::make_pair(kInvalid, 0);
}

IpAddress::IpAddress() : family_(AF_UNSPEC) { std::memset(&v6_, 0, sizeof(v6_)); }

IpAddress::IpAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) : family_(AF_INET) {
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&v4_.s_addr);
  bytes[0] = b0;
  bytes[1] = b1;
  bytes[2] = b2;
  bytes[3] = b3;
}

IpAddress::IpAddress(in_addr_t addr) : family_(AF_INET) { v4_.s_addr = addr; }

IpAddress::IpAddress(const in_addr& addr) : family_(AF_INET), v4_(addr) {}

IpAddress::IpAddress(uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3, uint16_t w4, uint16_t w5,
                     uint16_t w6, uint16_t w7)
    : family_(AF_INET6) {
  uint16_t* words = v6_.s6_addr16;
  words[0] = htobe16(w0);
  words[1] = htobe16(w1);
  words[2] = htobe16(w2);
  words[3] = htobe16(w3);
  words[4] = htobe16(w4);
  words[5] = htobe16(w5);
  words[6] = htobe16(w6);
  words[7] = htobe16(w7);
}

IpAddress::IpAddress(const std::vector<uint16_t>& source, size_t start) : family_(AF_INET6) {
  FX_DCHECK(start + source.size() <= kV6WordCount);

  uint16_t* words = v6_.s6_addr16;

  for (size_t i = 0; i < start; ++i) {
    words[i] = 0;
  }

  for (size_t i = start; i < start + source.size(); ++i) {
    words[i] = htobe16(source[i - start]);
  }

  for (size_t i = start + source.size(); i < kV6WordCount; ++i) {
    words[i] = 0;
  }
}

IpAddress::IpAddress(uint16_t w0, uint16_t w7) : family_(AF_INET6) {
  std::memset(&v6_, 0, sizeof(v6_));
  uint16_t* words = v6_.s6_addr16;
  words[0] = htobe16(w0);
  words[7] = htobe16(w7);
}

IpAddress::IpAddress(const in6_addr& addr) : family_(AF_INET6), v6_(addr) {}

IpAddress::IpAddress(const sockaddr& addr) {
  switch (addr.sa_family) {
    case AF_INET:
      family_ = AF_INET;
      v4_ = *reinterpret_cast<const in_addr*>(addr.sa_data);
      break;
    case AF_INET6:
      family_ = AF_INET6;
      v6_ = *reinterpret_cast<const in6_addr*>(addr.sa_data);
      break;
    default:
      family_ = AF_UNSPEC;
      std::memset(&v6_, 0, sizeof(v6_));
      break;
  }
}

IpAddress::IpAddress(const sockaddr_storage& addr) {
  switch (addr.ss_family) {
    case AF_INET:
      family_ = AF_INET;
      v4_ = *reinterpret_cast<const in_addr*>(reinterpret_cast<const uint8_t*>(&addr) +
                                              sizeof(sa_family_t));
      break;
    case AF_INET6:
      family_ = AF_INET6;
      v6_ = *reinterpret_cast<const in6_addr*>(reinterpret_cast<const uint8_t*>(&addr) +
                                               sizeof(sa_family_t));
      break;
    default:
      family_ = AF_UNSPEC;
      std::memset(&v6_, 0, sizeof(v6_));
      break;
  }
}

IpAddress::IpAddress(const fuchsia::net::Ipv4Address& addr) : family_(AF_INET) {
  std::copy(addr.addr.cbegin(), addr.addr.cend(), reinterpret_cast<uint8_t*>(&v4_));
}

IpAddress::IpAddress(const fuchsia::net::Ipv6Address& addr) : family_(AF_INET6) {
  std::copy(addr.addr.cbegin(), addr.addr.cend(), reinterpret_cast<uint8_t*>(&v6_));
}

IpAddress::IpAddress(const fuchsia::net::IpAddress& addr) {
  switch (addr.Which()) {
    case fuchsia::net::IpAddress::Tag::kIpv4:
      family_ = AF_INET;
      std::copy(addr.ipv4().addr.cbegin(), addr.ipv4().addr.cend(),
                reinterpret_cast<uint8_t*>(&v4_));
      break;
    case fuchsia::net::IpAddress::Tag::kIpv6:
      family_ = AF_INET6;
      std::copy(addr.ipv6().addr.cbegin(), addr.ipv6().addr.cend(),
                reinterpret_cast<uint8_t*>(&v6_));
      break;
    case fuchsia::net::IpAddress::Tag::Invalid:
      FX_DCHECK(false);
      break;
  }
}

bool IpAddress::is_mapped_from_v4() const { return is_v6() && IN6_IS_ADDR_V4MAPPED(&v6_); }

IpAddress IpAddress::mapped_v4_address() const {
  FX_DCHECK(is_mapped_from_v4());
  auto bytes = as_bytes();
  return IpAddress(bytes[12], bytes[13], bytes[14], bytes[15]);
}

IpAddress IpAddress::mapped_as_v6() const {
  FX_DCHECK(is_v4());
  auto bytes = as_bytes();
  // The words passed in to this constructor are stored in big-endian order.
  return IpAddress(0, 0, 0, 0, 0, 0xffff, static_cast<uint16_t>(bytes[0] << 8) | bytes[1],
                   static_cast<uint16_t>(bytes[2] << 8) | bytes[3]);
}

bool IpAddress::is_loopback() const {
  switch (family_) {
    case AF_INET:
      return *this == kV4Loopback;
    case AF_INET6:
      return *this == kV6Loopback;
    default:
      return false;
  }
}

bool IpAddress::is_link_local() const {
  if (!is_valid()) {
    return false;
  }

  switch (family_) {
    case AF_INET: {
      auto bytes = as_bytes();
      return bytes[0] == kV4LinkLocalFirstByte && bytes[1] == kV4LinkLocalSecondByte;
    }
    case AF_INET6:
      return IN6_IS_ADDR_LINKLOCAL(&v6_);
    default:
      return false;
  }
}

std::string IpAddress::ToString() const {
  std::ostringstream os;
  os << *this;
  return os.str();
}

std::ostream& operator<<(std::ostream& os, const IpAddress& value) {
  if (!value.is_valid()) {
    return os << "<invalid>";
  }

  if (value.is_v4()) {
    const uint8_t* bytes = value.as_bytes();
    return os << static_cast<int>(bytes[0]) << '.' << static_cast<int>(bytes[1]) << '.'
              << static_cast<int>(bytes[2]) << '.' << static_cast<int>(bytes[3]);
  } else {
    // IPV6 text representation per RFC 5952:
    // 1) Suppress leading zeros in hex representation of words.
    // 2) Don't use '::' to shorten a just single zero word.
    // 3) Shorten the longest sequence of zero words preferring the leftmost
    //    sequence if there's a tie.
    // 4) Use lower-case hexadecimal.

    const uint16_t* words = value.as_v6_words();

    // Figure out where the longest span of zeros is.
    uint8_t start_of_zeros;
    uint8_t zeros_seen = 0;
    uint8_t start_of_best_zeros = 255;
    // Don't bother if the longest sequence is length 1.
    uint8_t best_zeros_seen = 1;

    for (uint8_t i = 0; i < kV6WordCount; ++i) {
      if (words[i] == 0) {
        if (zeros_seen == 0) {
          start_of_zeros = i;
        }
        ++zeros_seen;
      } else if (zeros_seen != 0) {
        if (zeros_seen > best_zeros_seen) {
          start_of_best_zeros = start_of_zeros;
          best_zeros_seen = zeros_seen;
        }
        zeros_seen = 0;
      }
    }

    if (zeros_seen > best_zeros_seen) {
      start_of_best_zeros = start_of_zeros;
      best_zeros_seen = zeros_seen;
    }

    os << std::hex;
    for (uint8_t i = 0; i < kV6WordCount; ++i) {
      if (i < start_of_best_zeros || i >= start_of_best_zeros + best_zeros_seen) {
        os << be16toh(words[i]);
        if (i != 7) {
          os << ":";
        }
      } else if (i == start_of_best_zeros) {
        if (i == 0) {
          os << "::";
        } else {
          os << ":";  // We just wrote a ':', so we only need one more.
        }
      }
    }
    return os << std::dec;
  }
}

IpAddress::operator fuchsia::net::Ipv4Address() const {
  FX_DCHECK(is_v4());
  auto v4_ptr = reinterpret_cast<const uint8_t*>(&v4_);
  fuchsia::net::Ipv4Address result;
  std::copy(v4_ptr, v4_ptr + sizeof(result.addr), result.addr.begin());
  return result;
}

IpAddress::operator fuchsia::net::Ipv6Address() const {
  FX_DCHECK(is_v6());
  auto v6_ptr = reinterpret_cast<const uint8_t*>(&v6_);
  fuchsia::net::Ipv6Address result;
  std::copy(v6_ptr, v6_ptr + sizeof(result.addr), result.addr.begin());
  return result;
}

IpAddress::operator fuchsia::net::IpAddress() const {
  if (is_v4()) {
    return fuchsia::net::IpAddress::WithIpv4(static_cast<fuchsia::net::Ipv4Address>(*this));
  }

  return fuchsia::net::IpAddress::WithIpv6(static_cast<fuchsia::net::Ipv6Address>(*this));
}

}  // namespace inet

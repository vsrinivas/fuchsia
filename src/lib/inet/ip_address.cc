// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inet/ip_address.h"

#include <arpa/inet.h>
#include <endian.h>
#include <netdb.h>
#include <sys/socket.h>

#include <sstream>

namespace inet {
namespace {

// Parses a string. Match functions either return true and update the position
// of the parser or return false and leave the position unchanged.
class Parser {
 public:
  Parser(const std::string& str) : str_(str), pos_(0) {}

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
    FXL_DCHECK(out);

    if (pos_ == str_.length() || !std::isdigit(static_cast<unsigned char>(str_[pos_]))) {
      return false;
    }

    *out = str_[pos_] - '0';
    ++pos_;

    return true;
  }

  // Matches a single lowercase hexadecimal digit.
  bool MatchLowerHexDigit(uint8_t* out) {
    FXL_DCHECK(out);

    if (pos_ == str_.length() || !std::isxdigit(static_cast<unsigned char>(str_[pos_]))) {
      return false;
    }

    if (std::isdigit(static_cast<unsigned char>(str_[pos_]))) {
      *out = str_[pos_] - '0';
    } else if (std::islower(static_cast<unsigned char>(str_[pos_]))) {
      *out = 10 + (str_[pos_] - 'a');
    } else {
      // Uppercase hexadecimal is not permitted.
      return false;
    }

    ++pos_;

    return true;
  }

  // Matches a decimal byte of at most 3 digits. The match will succeed even
  // if the decimal byte is followed immediately by a digit. If matching three
  // digits would produce a value greater than 255, only two digits are matched.
  bool MatchMax3DigitDecByte(uint8_t* byte_out) {
    FXL_DCHECK(byte_out);

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

    FXL_DCHECK(accum <= 255);
    *byte_out = static_cast<uint8_t>(accum);
    return true;
  }

  // Matches a lowercase hexadecimal word of at most 4 digits. The match will
  // succeed even if the hexadecimal word is followed immediately by a
  // hexadecimal digit.
  bool MatchMax4DigitLowerHexWord(uint16_t* word_out) {
    FXL_DCHECK(word_out);

    uint8_t digit = 0;
    if (!MatchLowerHexDigit(&digit)) {
      return false;
    }

    uint16_t accum = digit;
    if (MatchLowerHexDigit(&digit)) {
      accum = accum * 16 + digit;
      if (MatchLowerHexDigit(&digit)) {
        accum = accum * 16 + digit;
        if (MatchLowerHexDigit(&digit)) {
          accum = accum * 16 + digit;
        }
      }
    }

    *word_out = accum;
    return true;
  }

  // Matches an IPV4 address.
  bool MatchIpV4Address(IpAddress* address_out) {
    FXL_DCHECK(address_out);

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
    FXL_DCHECK(address_out);

    size_t old_pos = pos_;
    uint16_t words[8];
    size_t word_index = 0;
    size_t ellipsis_word_index = 0;

    if (MatchMax4DigitLowerHexWord(&words[word_index]) && Match(':')) {
      while (true) {
        // At this point, we've matched at least one word, we've just matched a
        // colon, and |word_index| indexes the last word we matched.
        ++word_index;

        // Check for "::" ellipsis.
        if (Match(':')) {
          if (ellipsis_word_index != 0) {
            // More than one "::" ellipsis.
            break;
          }

          ellipsis_word_index = word_index;
        }

        if (MatchMax4DigitLowerHexWord(&words[word_index])) {
          if (word_index < 7 && Match(':')) {
            // More words to read.
            continue;
          }
        } else if (word_index == 1) {
          // Need at least two words.
          break;
        } else {
          // We've read a ':' past the end.
          --pos_;
        }

        if (word_index == 7) {
          if (ellipsis_word_index != 0) {
            // We parsed 8 words, and there's an ellipsis.
            break;
          }
        } else {
          if (ellipsis_word_index == 0) {
            // We parsed less than 8 words, and there's no ellipsis.
            break;
          }

          // Insert zeros for the ellipsis.
          size_t to = 7;
          for (size_t from = word_index; from >= ellipsis_word_index; --from) {
            words[to] = words[from];
            --to;
          }

          for (; to >= ellipsis_word_index; --to) {
            words[to] = 0;
          }
        }

        *address_out = IpAddress(words[0], words[1], words[2], words[3], words[4], words[5],
                                 words[6], words[7]);
        return true;
      }
    }

    pos_ = old_pos;
    return false;
  }

 private:
  const std::string& str_;
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
IpAddress IpAddress::FromString(const std::string address_string, sa_family_t family) {
  FXL_DCHECK(family == AF_UNSPEC || family == AF_INET || family == AF_INET6);

  Parser parser(address_string);
  IpAddress address;
  if ((parser.MatchIpV4Address(&address) || parser.MatchIpV6Address(&address)) &&
      parser.MatchEnd()) {
    return address;
  }

  return kInvalid;
}

IpAddress::IpAddress() {
  family_ = AF_UNSPEC;
  std::memset(&v6_, 0, sizeof(v6_));
}

IpAddress::IpAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  family_ = AF_INET;
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&v4_.s_addr);
  bytes[0] = b0;
  bytes[1] = b1;
  bytes[2] = b2;
  bytes[3] = b3;
}

IpAddress::IpAddress(in_addr_t addr) {
  family_ = AF_INET;
  v4_.s_addr = addr;
}

IpAddress::IpAddress(const in_addr& addr) {
  family_ = AF_INET;
  v4_ = addr;
}

IpAddress::IpAddress(uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3, uint16_t w4, uint16_t w5,
                     uint16_t w6, uint16_t w7) {
  family_ = AF_INET6;
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

IpAddress::IpAddress(uint16_t w0, uint16_t w7) {
  family_ = AF_INET6;
  std::memset(&v6_, 0, sizeof(v6_));
  uint16_t* words = v6_.s6_addr16;
  words[0] = htobe16(w0);
  words[7] = htobe16(w7);
}

IpAddress::IpAddress(const in6_addr& addr) {
  family_ = AF_INET6;
  v6_ = addr;
}

IpAddress::IpAddress(const sockaddr* addr) {
  FXL_DCHECK(addr != nullptr);
  switch (addr->sa_family) {
    case AF_INET:
      family_ = AF_INET;
      v4_ = *reinterpret_cast<const in_addr*>(addr->sa_data);
      break;
    case AF_INET6:
      family_ = AF_INET6;
      v6_ = *reinterpret_cast<const in6_addr*>(addr->sa_data);
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

IpAddress::IpAddress(const fuchsia::net::Ipv4Address* addr) {
  FXL_DCHECK(addr != nullptr);
  family_ = AF_INET;
  memcpy(&v4_, addr->addr.data(), 4);
}

IpAddress::IpAddress(const fuchsia::net::Ipv6Address* addr) {
  FXL_DCHECK(addr != nullptr);
  family_ = AF_INET6;
  memcpy(&v6_, addr->addr.data(), 16);
}

IpAddress::IpAddress(const fuchsia::net::IpAddress* addr) {
  FXL_DCHECK(addr != nullptr);
  switch (addr->Which()) {
    case fuchsia::net::IpAddress::Tag::kIpv4:
      family_ = AF_INET;
      memcpy(&v4_, addr->ipv4().addr.data(), 4);
      break;
    case fuchsia::net::IpAddress::Tag::kIpv6:
      family_ = AF_INET6;
      memcpy(&v6_, addr->ipv6().addr.data(), 16);
      break;
    default:
      FXL_DCHECK(false);
      break;
  }
}

bool IpAddress::is_mapped_from_v4() const {
  // A V6 address mapped from a V4 address takes the form 0::ffff:xxxx:xxxx, where the x's make
  // up the V4 address.
  return is_v6() && v6_.s6_addr16[0] == 0 && v6_.s6_addr16[1] == 0 && v6_.s6_addr16[2] == 0 &&
         v6_.s6_addr16[3] == 0 && v6_.s6_addr16[4] == 0 && v6_.s6_addr16[5] == 0xffff;
}

IpAddress IpAddress::mapped_v4_address() const {
  FXL_DCHECK(is_mapped_from_v4());
  auto bytes = as_bytes();
  return IpAddress(bytes[12], bytes[13], bytes[14], bytes[15]);
}

IpAddress IpAddress::mapped_as_v6() const {
  FXL_DCHECK(is_v4());
  auto bytes = as_bytes();
  // The words passed in to this constructor are stored in big-endian order.
  return IpAddress(0, 0, 0, 0, 0, 0xffff, static_cast<uint16_t>(bytes[0]) << 8 | bytes[1],
                   static_cast<uint16_t>(bytes[2]) << 8 | bytes[3]);
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

    const uint16_t* words = value.as_words();

    // Figure out where the longest span of zeros is.
    uint8_t start_of_zeros;
    uint8_t zeros_seen = 0;
    uint8_t start_of_best_zeros = 255;
    // Don't bother if the longest sequence is length 1.
    uint8_t best_zeros_seen = 1;

    for (uint8_t i = 0; i < 8; ++i) {
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
    for (uint8_t i = 0; i < 8; ++i) {
      if (i < start_of_best_zeros || i >= start_of_best_zeros + best_zeros_seen) {
        os << betoh16(words[i]);
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

}  // namespace inet

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tokens.h"

#include <zircon/assert.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace netdump {

TokenPtr Tokenizer::keyword(const std::string& term, uint64_t tag /*= 0*/) {
  auto iter = dictionary_.find(term);
  if (iter != dictionary_.end()) {
    uint64_t old_tag = iter->second->get_tag<uint64_t>();
    ZX_DEBUG_ASSERT_MSG(tag == old_tag, "Redefinition of tag value for '%s', old: %lu, new: %lu",
                        iter->second->get_term().c_str(), old_tag, tag);
    return iter->second;
  }

  TokenPtr token = fbl::AdoptRef(new TokenBase(term, tag));
  dictionary_[term] = token;
  return token;
}

TokenPtr Tokenizer::keyword(const std::string& term, const std::string& synonym,
                            uint64_t tag /*= 0*/) {
  TokenPtr token = keyword(term, tag);
  uint64_t old_tag = token->get_tag<uint64_t>();
  ZX_DEBUG_ASSERT_MSG(tag == old_tag, "Redefinition of tag value for '%s', old: %lu, new: %lu",
                      token->get_term().c_str(), old_tag, tag);
  dictionary_[synonym] = token;
  return token;
}

TokenPtr Tokenizer::literal(const std::string& term) const {
  auto iter = dictionary_.find(term);
  if (iter != dictionary_.end()) {
    return iter->second;
  }
  // `tag` is only meaningful for keywords so use default value.
  return fbl::AdoptRef(new TokenBase(term, 0));
}

std::vector<TokenPtr> Tokenizer::tokenize(const std::string& filter_string) const {
  std::vector<TokenPtr> tokens;
  std::istringstream stream(filter_string);
  std::istream_iterator<std::string> begin(stream);
  std::istream_iterator<std::string> end;
  for (; begin != end; ++begin) {
    tokens.emplace_back(literal(*begin));
  }
  return tokens;
}

static bool port_num(const char* input, size_t len, uint16_t* num) {
  char* num_end = nullptr;
  long int input_num = strtol(input, &num_end, 10);  // Input number is expected to be base-10.
  if (input_num < 0 ||                               // Negative number.
      input_num > std::numeric_limits<uint16_t>::max() ||  // Number too big.
      num_end == input ||                                  // Did not consume any characters.
      num_end < input + len) {                             // Did not consume whole string.
    // Not a valid port number.
    return false;
  }
  *num = static_cast<uint16_t>(input_num);
  return true;
}

TokenPtr Tokenizer::port(const std::string& port_string) const {
  // If a named port is used, the whole `port_string` must be the name.
  auto iter = dictionary_.find(port_string);
  if (iter != dictionary_.end()) {
    return iter->second;
  }

  uint16_t begin_port = 0;
  uint16_t end_port = 0;
  const char* input = port_string.c_str();
  size_t divider = port_string.find('-');
  if (divider == std::string::npos) {
    // A single number.
    if (!port_num(input, port_string.length(), &begin_port)) {
      // Not a valid port.
      return fbl::AdoptRef(new TokenBase(port_string, 0));
    }
    end_port = begin_port;
  } else {
    // A range.
    const char* end_port_start = input + divider + 1;
    size_t end_port_str_len = port_string.length() - divider - 1;
    ZX_DEBUG_ASSERT_MSG(port_string.length() == divider + end_port_str_len + 1,
                        "Unexpected port string lengths: %zu and %lu", divider, end_port_str_len);
    if (!port_num(input, divider, &begin_port) ||
        !port_num(end_port_start, end_port_str_len, &end_port) || end_port < begin_port) {
      // Begin or end port string was invalid.
      return fbl::AdoptRef(new TokenBase(port_string, 0));
    }
  }

  return fbl::AdoptRef(new PortToken(begin_port, end_port, 0));
}

std::vector<TokenPtr> Tokenizer::mult_ports(char delim, const std::string& ports_list) const {
  std::stringstream stream(ports_list);
  std::string port_string;
  std::vector<TokenPtr> tokens;

  while (std::getline(stream, port_string, delim)) {
    tokens.push_back(port(port_string));
  }

  return tokens;
}

TokenPtr Tokenizer::named_port(const std::string& name, uint16_t begin, uint16_t end,
                               uint64_t tag /*= 0*/) {
  auto iter = dictionary_.find(name);
  if (iter != dictionary_.end()) {
    return iter->second;
  }

  TokenPtr token = AdoptRef(new PortToken(name, begin, end, tag));
  dictionary_[name] = token;
  return token;
}

TokenPtr Tokenizer::named_port(const std::string& name, const std::string& synonym, uint16_t begin,
                               uint16_t end, uint64_t tag /*= 0*/) {
  TokenPtr token = named_port(name, begin, end, tag);
  dictionary_[synonym] = token;
  return token;
}

}  // namespace netdump

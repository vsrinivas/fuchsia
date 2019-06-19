// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the syntax logic for the packet filter language.

#pragma once

#include <arpa/inet.h>
#include <netinet/if_ether.h>

#include "filter_builder.h"
#include "parser_state.h"

namespace netdump::parser {

template <class T>
class Syntax {
public:
    // Attempt a parse by recursive descent. The parse state is tracked in `env`.
    // `parens` should be true if the parse is under parenthesis, otherwise it should be false.
    // Return null if the specification is invalid. On return, the `env` error data is updated if
    // there was a syntax mistake.
    std::optional<T> parse(bool parens, const Tokenizer& tkz, Environment* env,
                           FilterBuilder<T>* builder) {
        // TODO(xianglong): Implement.
        return std::nullopt;
    }
};

} // namespace netdump::parser

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

namespace netdump::parser {

void parser_syntax(std::ostream* output) {
  ZX_DEBUG_ASSERT_MSG(output != nullptr, "Output stream for parser syntax was null.");
#define BOLD(x) ANSI_BOLD << (x) << ANSI_RESET
#define ENDL std::endl
  // clang-format off
    (*output)
        << "       expr ::= " << BOLD("(") << " expr " << BOLD(")")                         << ENDL
        << "              | " << BOLD("not") << " expr  |"
                              << " expr " << BOLD("and") << " expr |"
                              << " expr " << BOLD("or") << " expr"                          << ENDL
        << "              | eth_expr  | host_expr     | trans_expr"                         << ENDL
        << "length_expr ::= " << BOLD("greater") << " <len> | " << BOLD("less") << " <len>" << ENDL
        << "       type ::= " << BOLD("src") << " | " << BOLD("dst")                        << ENDL
        << "   eth_expr ::= length_expr"                                                    << ENDL
        << "              | "<< BOLD("ether") << " [type] " << BOLD("host") << " <mac_addr>"<< ENDL
        << "              | [" << BOLD("ether") << " " << BOLD("proto") << "] net_expr"     << ENDL
        << "   net_expr ::= " << BOLD("arp")                                                << ENDL
        << "              | " << BOLD("vlan")                                               << ENDL
        << "              | " << BOLD("ip") << "  [length_expr | host_expr | trans_expr]"   << ENDL
        << "              | " << BOLD("ip6") << " [length_expr | host_expr | trans_expr]"   << ENDL
        << "  host_expr ::= [type] " << BOLD("host") << " <ip_addr>"                        << ENDL
        << " trans_expr ::= [" << BOLD("proto") << "] " << BOLD("icmp")                     << ENDL
        << "              | [" << BOLD("proto") << "] " << BOLD("tcp") << " [port_expr]"    << ENDL
        << "              | [" << BOLD("proto") << "] " << BOLD("udp") << " [port_expr]"    << ENDL
        << "  port_expr ::= [type] " << BOLD("port") << " <port_lst>"                       << ENDL;
  // clang-format on
#undef ENDL
#undef BOLD
}

}  // namespace netdump::parser

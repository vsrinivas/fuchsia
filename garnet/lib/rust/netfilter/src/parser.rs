// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use pest::iterators::Pair;
use pest::Parser;
use pest_derive::Parser;

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_filter as filter;

#[derive(Parser)]
#[grammar_inline = r#"
rules = { SOI ~ (rule ~ ";")+ ~ EOI }

rule = {
     action ~
     direction ~
     quick ~
     proto ~
     src ~
     dst ~
     log ~
     state
}

nat_rules = { SOI ~ (nat ~ ";")+ ~ EOI }

nat = {
     "nat" ~
     proto ~
     "from" ~ subnet ~
     "->"  ~
     "from" ~ ipaddr
}

rdr_rules = { SOI ~ (rdr ~ ";")+ ~ EOI }

rdr = {
     "rdr" ~
     proto ~
     "to" ~ ipaddr ~ "port" ~ port ~
     "->" ~
     "to" ~ ipaddr ~ "port" ~ port
}

action = { pass | drop | dropreset }
  pass = { "pass" }
  drop = { "drop" }
  dropreset = { "dropreset" }

direction = { incoming | outgoing }
  incoming = { "in" }
  outgoing = { "out" }

quick = { ("quick")? }

proto = { "proto" ~ (tcp | udp | icmp) }
  tcp = { "tcp" }
  udp = { "udp" }
  icmp = { "icmp" }

src = { ("from" ~ invertible_subnet? ~ ("port" ~ port)?)? }
dst = { ("to" ~ invertible_subnet? ~ ("port" ~ port)?)? }

invertible_subnet = { not? ~ subnet }
  not = { "!" }

subnet = ${ ipaddr ~ "/" ~ prefix_len }

ipaddr = { ipv4addr | ipv6addr }
  ipv4addr = @{ ASCII_DIGIT{1,3} ~ ("." ~ ASCII_DIGIT{1,3}){3} }
  ipv6addr = @{ (ASCII_HEX_DIGIT | ":")+ }

prefix_len = @{ ASCII_DIGIT+ }

port = @{ ASCII_DIGIT+ }

log = { ("log")? }

state = { (state_adj ~ "state")? }
  state_adj = { "keep" | "no" }

WHITESPACE = _{ " " }
"#]

pub struct FilterRuleParser;

#[derive(Debug)]
pub enum Error {
    Pest(pest::error::Error<Rule>),
    Addr(std::net::AddrParseError),
    Num(std::num::ParseIntError),
}

fn parse_action(pair: Pair<Rule>) -> filter::Action {
    assert_eq!(pair.as_rule(), Rule::action);
    match pair.into_inner().next().unwrap().as_rule() {
        Rule::pass => filter::Action::Pass,
        Rule::drop => filter::Action::Drop,
        Rule::dropreset => filter::Action::DropReset,
        _ => unreachable!(),
    }
}

fn parse_direction(pair: Pair<Rule>) -> filter::Direction {
    assert_eq!(pair.as_rule(), Rule::direction);
    match pair.into_inner().next().unwrap().as_rule() {
        Rule::incoming => filter::Direction::Incoming,
        Rule::outgoing => filter::Direction::Outgoing,
        _ => unreachable!(),
    }
}

fn parse_quick(pair: Pair<Rule>) -> bool {
    assert_eq!(pair.as_rule(), Rule::quick);
    pair.as_str() == "quick"
}

fn parse_proto(pair: Pair<Rule>) -> filter::SocketProtocol {
    assert_eq!(pair.as_rule(), Rule::proto);
    match pair.into_inner().next().unwrap().as_rule() {
        Rule::tcp => filter::SocketProtocol::Tcp,
        Rule::udp => filter::SocketProtocol::Udp,
        Rule::icmp => filter::SocketProtocol::Icmp,
        _ => unreachable!(),
    }
}

fn parse_src(pair: Pair<Rule>) -> Result<(Option<Box<net::Subnet>>, bool, u16), Error> {
    assert_eq!(pair.as_rule(), Rule::src);
    parse_src_or_dst(pair)
}

fn parse_dst(pair: Pair<Rule>) -> Result<(Option<Box<net::Subnet>>, bool, u16), Error> {
    assert_eq!(pair.as_rule(), Rule::dst);
    parse_src_or_dst(pair)
}

fn parse_src_or_dst(pair: Pair<Rule>) -> Result<(Option<Box<net::Subnet>>, bool, u16), Error> {
    let mut inner = pair.into_inner();
    let (subnet, invert_match, port) = match inner.next() {
        Some(pair) => match pair.as_rule() {
            Rule::invertible_subnet => {
                let (subnet, invert_match) = parse_invertible_subnet(pair)?;
                let port = match inner.next() {
                    Some(pair) => parse_port(pair)?,
                    None => 0,
                };
                (Some(Box::new(subnet)), invert_match, port)
            }
            Rule::port => (None, false, parse_port(pair)?),
            _ => unreachable!(),
        },
        None => (None, false, 0),
    };
    Ok((subnet, invert_match, port))
}

fn parse_invertible_subnet(pair: Pair<Rule>) -> Result<(net::Subnet, bool), Error> {
    assert_eq!(pair.as_rule(), Rule::invertible_subnet);
    let mut inner = pair.into_inner();
    let mut pair = inner.next().unwrap();
    let invert_match = match pair.as_rule() {
        Rule::not => {
            pair = inner.next().unwrap();
            true
        }
        Rule::subnet => false,
        _ => unreachable!(),
    };
    let subnet = parse_subnet(pair)?;
    Ok((subnet, invert_match))
}

fn parse_subnet(pair: Pair<Rule>) -> Result<net::Subnet, Error> {
    assert_eq!(pair.as_rule(), Rule::subnet);
    let mut inner = pair.into_inner();
    let addr = parse_ipaddr(inner.next().unwrap())?;
    let prefix_len = parse_prefix_len(inner.next().unwrap())?;

    Ok(net::Subnet { addr: addr, prefix_len: prefix_len })
}

fn parse_ipaddr(pair: Pair<Rule>) -> Result<net::IpAddress, Error> {
    assert_eq!(pair.as_rule(), Rule::ipaddr);
    let pair = pair.into_inner().next().unwrap();
    let addr = pair.as_str().parse().map_err(Error::Addr)?;
    match addr {
        std::net::IpAddr::V4(ip4) => {
            Ok(net::IpAddress::Ipv4(net::Ipv4Address { addr: ip4.octets() }))
        }
        std::net::IpAddr::V6(ip6) => {
            Ok(net::IpAddress::Ipv6(net::Ipv6Address { addr: ip6.octets() }))
        }
    }
}

fn parse_prefix_len(pair: Pair<Rule>) -> Result<u8, Error> {
    assert_eq!(pair.as_rule(), Rule::prefix_len);
    pair.as_str().parse::<u8>().map_err(Error::Num)
}

fn parse_port(pair: Pair<Rule>) -> Result<u16, Error> {
    assert_eq!(pair.as_rule(), Rule::port);
    pair.as_str().parse::<u16>().map_err(Error::Num)
}

fn parse_log(pair: Pair<Rule>) -> bool {
    assert_eq!(pair.as_rule(), Rule::log);
    pair.as_str() == "log"
}

fn parse_state(pair: Pair<Rule>) -> bool {
    assert_eq!(pair.as_rule(), Rule::state);
    let mut inner = pair.into_inner();
    match inner.next() {
        Some(pair) => {
            assert_eq!(pair.as_rule(), Rule::state_adj);
            match pair.as_str() {
                "no" => false,
                "keep" => true,
                _ => unreachable!(),
            }
        }
        None => true, // keep state by default
    }
}

fn parse_rule(pair: Pair<Rule>) -> Result<filter::Rule, Error> {
    assert_eq!(pair.as_rule(), Rule::rule);
    let mut pairs = pair.into_inner();

    let action = parse_action(pairs.next().unwrap());
    let direction = parse_direction(pairs.next().unwrap());
    let quick = parse_quick(pairs.next().unwrap());
    let proto = parse_proto(pairs.next().unwrap());
    let (src_subnet, src_subnet_invert_match, src_port) = parse_src(pairs.next().unwrap())?;
    let (dst_subnet, dst_subnet_invert_match, dst_port) = parse_dst(pairs.next().unwrap())?;
    let log = parse_log(pairs.next().unwrap());
    let keep_state = parse_state(pairs.next().unwrap());

    Ok(filter::Rule {
        action: action,
        direction: direction,
        quick: quick,
        proto: proto,
        src_subnet: src_subnet,
        src_subnet_invert_match: src_subnet_invert_match,
        src_port: src_port,
        dst_subnet: dst_subnet,
        dst_subnet_invert_match: dst_subnet_invert_match,
        dst_port: dst_port,
        nic: 0, // TODO: Support NICID (currently always 0 (= any))
        log: log,
        keep_state: keep_state,
    })
}

fn parse_nat(pair: Pair<Rule>) -> Result<filter::Nat, Error> {
    assert_eq!(pair.as_rule(), Rule::nat);
    let mut pairs = pair.into_inner();

    let proto = parse_proto(pairs.next().unwrap());
    let src_subnet = parse_subnet(pairs.next().unwrap())?;
    let new_src_addr = parse_ipaddr(pairs.next().unwrap())?;

    Ok(filter::Nat {
        proto: proto,
        src_subnet: src_subnet,
        new_src_addr: new_src_addr,
        nic: 0, // TODO: Support NICID.
    })
}

fn parse_rdr(pair: Pair<Rule>) -> Result<filter::Rdr, Error> {
    assert_eq!(pair.as_rule(), Rule::rdr);
    let mut pairs = pair.into_inner();

    let proto = parse_proto(pairs.next().unwrap());
    let dst_addr = parse_ipaddr(pairs.next().unwrap())?;
    let dst_port = parse_port(pairs.next().unwrap())?;
    let new_dst_addr = parse_ipaddr(pairs.next().unwrap())?;
    let new_dst_port = parse_port(pairs.next().unwrap())?;

    Ok(filter::Rdr {
        proto: proto,
        dst_addr: dst_addr,
        dst_port: dst_port,
        new_dst_addr: new_dst_addr,
        new_dst_port: new_dst_port,
        nic: 0, // TODO: Support NICID.
    })
}

pub fn parse_str_to_rules(line: &str) -> Result<Vec<filter::Rule>, Error> {
    let mut pairs = FilterRuleParser::parse(Rule::rules, &line).map_err(Error::Pest)?;
    let mut rules = Vec::new();
    for filter_rule in pairs.next().unwrap().into_inner() {
        match filter_rule.as_rule() {
            Rule::rule => {
                rules.push(parse_rule(filter_rule)?);
            }
            Rule::EOI => (),
            _ => unreachable!(),
        }
    }
    Ok(rules)
}

pub fn parse_str_to_nat_rules(line: &str) -> Result<Vec<filter::Nat>, Error> {
    let mut pairs = FilterRuleParser::parse(Rule::nat_rules, &line).map_err(Error::Pest)?;
    let mut nat_rules = Vec::new();
    for filter_rule in pairs.next().unwrap().into_inner() {
        match filter_rule.as_rule() {
            Rule::nat => {
                nat_rules.push(parse_nat(filter_rule)?);
            }
            Rule::EOI => (),
            _ => unreachable!(),
        }
    }
    Ok(nat_rules)
}

pub fn parse_str_to_rdr_rules(line: &str) -> Result<Vec<filter::Rdr>, Error> {
    let mut pairs = FilterRuleParser::parse(Rule::rdr_rules, &line).map_err(Error::Pest)?;
    let mut rdr_rules = Vec::new();
    for filter_rule in pairs.next().unwrap().into_inner() {
        match filter_rule.as_rule() {
            Rule::rdr => {
                rdr_rules.push(parse_rdr(filter_rule)?);
            }
            Rule::EOI => (),
            _ => unreachable!(),
        }
    }
    Ok(rdr_rules)
}

#[cfg(test)]
mod test {
    use super::*;

    fn test_parse_line_to_rules(line: &str, expected: &[filter::Rule]) {
        let mut iter = expected.iter();
        match FilterRuleParser::parse(Rule::rules, &line) {
            Ok(mut pairs) => {
                for filter_rule in pairs.next().unwrap().into_inner() {
                    match filter_rule.as_rule() {
                        Rule::rule => {
                            assert_eq!(parse_rule(filter_rule).unwrap(), *iter.next().unwrap());
                        }
                        Rule::EOI => (),
                        _ => unreachable!(),
                    }
                }
            }
            Err(e) => panic!("Parse error: {}", e),
        }
    }

    fn test_parse_line_to_nat_rules(line: &str, expected: &[filter::Nat]) {
        let mut iter = expected.iter();
        match FilterRuleParser::parse(Rule::nat_rules, &line) {
            Ok(mut pairs) => {
                for filter_rule in pairs.next().unwrap().into_inner() {
                    match filter_rule.as_rule() {
                        Rule::nat => {
                            assert_eq!(parse_nat(filter_rule).unwrap(), *iter.next().unwrap());
                        }
                        Rule::EOI => (),
                        _ => unreachable!(),
                    }
                }
            }
            Err(e) => panic!("Parse error: {}", e),
        }
    }

    fn test_parse_line_to_rdr_rules(line: &str, expected: &[filter::Rdr]) {
        let mut iter = expected.iter();
        match FilterRuleParser::parse(Rule::rdr_rules, &line) {
            Ok(mut pairs) => {
                for filter_rule in pairs.next().unwrap().into_inner() {
                    match filter_rule.as_rule() {
                        Rule::rdr => {
                            assert_eq!(parse_rdr(filter_rule).unwrap(), *iter.next().unwrap());
                        }
                        Rule::EOI => (),
                        _ => unreachable!(),
                    }
                }
            }
            Err(e) => panic!("Parse error: {}", e),
        }
    }

    #[test]
    fn test_lines() {
        test_parse_line_to_rules(
            "pass in proto tcp;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port: 0,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp; drop out proto udp;",
            &[
                filter::Rule {
                    action: filter::Action::Pass,
                    direction: filter::Direction::Incoming,
                    quick: false,
                    proto: filter::SocketProtocol::Tcp,
                    src_subnet: None,
                    src_subnet_invert_match: false,
                    src_port: 0,
                    dst_subnet: None,
                    dst_subnet_invert_match: false,
                    dst_port: 0,
                    nic: 0,
                    log: false,
                    keep_state: true,
                },
                filter::Rule {
                    action: filter::Action::Drop,
                    direction: filter::Direction::Outgoing,
                    quick: false,
                    proto: filter::SocketProtocol::Udp,
                    src_subnet: None,
                    src_subnet_invert_match: false,
                    src_port: 0,
                    dst_subnet: None,
                    dst_subnet_invert_match: false,
                    dst_port: 0,
                    nic: 0,
                    log: false,
                    keep_state: true,
                },
            ],
        );
        test_parse_line_to_rules(
            "pass in proto tcp from 1.2.3.4/24;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 24,
                })),
                src_subnet_invert_match: false,
                src_port: 0,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp from port 10000;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port: 10000,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp from 1.2.3.4/24 port 10000;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 24,
                })),
                src_subnet_invert_match: false,
                src_port: 10000,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp from !1.2.3.4/24 port 10000;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 24,
                })),
                src_subnet_invert_match: true,
                src_port: 10000,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp from 1234:5678::/32 port 10000;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    }),
                    prefix_len: 32,
                })),
                src_subnet_invert_match: false,
                src_port: 10000,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp to 1234:5678::/32 port 10000;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port: 0,
                dst_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    }),
                    prefix_len: 32,
                })),
                dst_subnet_invert_match: false,
                dst_port: 10000,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp from 1234:5678::/32 port 10000 to 1.2.3.4/8 port 1000;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    }),
                    prefix_len: 32,
                })),
                src_subnet_invert_match: false,
                src_port: 10000,
                dst_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 8,
                })),
                dst_subnet_invert_match: false,
                dst_port: 1000,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_rules(
            "pass in proto tcp log no state;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port: 0,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: true,
                keep_state: false,
            }],
        );
        test_parse_line_to_rules(
            "pass in quick proto tcp keep state;",
            &[filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: true,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port: 0,
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: 0,
                nic: 0,
                log: false,
                keep_state: true,
            }],
        );
        test_parse_line_to_nat_rules(
            "nat proto tcp from 1.2.3.0/24 -> from 192.168.1.1;",
            &[filter::Nat {
                proto: filter::SocketProtocol::Tcp,
                src_subnet: net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 0] }),
                    prefix_len: 24,
                },
                new_src_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                nic: 0,
            }],
        );
        test_parse_line_to_rdr_rules(
            "rdr proto tcp to 1.2.3.4 port 10000 -> to 192.168.1.1 port 20000;",
            &[filter::Rdr {
                proto: filter::SocketProtocol::Tcp,
                dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                dst_port: 10000,
                new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                new_dst_port: 20000,
                nic: 0,
            }],
        );
        test_parse_line_to_rdr_rules(
            "rdr proto tcp to 1.2.3.4 port 1 -> to 192.168.1.1 port 20000;",
            &[filter::Rdr {
                proto: filter::SocketProtocol::Tcp,
                dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                dst_port: 1,
                new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                new_dst_port: 20000,
                nic: 0,
            }],
        );
    }
}

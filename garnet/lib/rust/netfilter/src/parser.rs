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
     "to" ~ ipaddr ~ port_range ~
     "->" ~
     "to" ~ ipaddr ~ port_range
}

action = { pass | drop | dropreset }
  pass = { "pass" }
  drop = { "drop" }
  dropreset = { "dropreset" }

direction = { incoming | outgoing }
  incoming = { "in" }
  outgoing = { "out" }

quick = { ("quick")? }

proto = { ("proto" ~ (tcp | udp | icmp))? }
  tcp = { "tcp" }
  udp = { "udp" }
  icmp = { "icmp" }

src = { ("from" ~ invertible_subnet? ~ port_range?)? }
dst = { ("to" ~ invertible_subnet? ~ port_range?)? }

invertible_subnet = { not? ~ subnet }
  not = { "!" }

subnet = ${ ipaddr ~ "/" ~ prefix_len }

ipaddr = { ipv4addr | ipv6addr }
  ipv4addr = @{ ASCII_DIGIT{1,3} ~ ("." ~ ASCII_DIGIT{1,3}){3} }
  ipv6addr = @{ (ASCII_HEX_DIGIT | ":")+ }

prefix_len = @{ ASCII_DIGIT+ }

port_range = { port ~ port_num | range ~ port_num ~ ":" ~ port_num }
  port = { "port" }
  range = { "range" }
  port_num = @{ ASCII_DIGIT+ }

log = { ("log")? }

state = { (state_adj ~ "state")? }
  state_adj = { "keep" | "no" }

WHITESPACE = _{ " " }
"#]

pub struct FilterRuleParser;

#[derive(Debug, PartialEq)]
pub enum Error {
    Pest(pest::error::Error<Rule>),
    Addr(std::net::AddrParseError),
    Num(std::num::ParseIntError),
    Invalid(InvalidReason),
}

#[derive(Debug, PartialEq)]
pub enum InvalidReason {
    MixedIPVersions,
    InvalidPortRange,
    PortRangeLengthMismatch,
}

impl std::fmt::Display for InvalidReason {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::MixedIPVersions => write!(f, "mixed IP versions"),
            Self::InvalidPortRange => write!(f, "invalid port range"),
            Self::PortRangeLengthMismatch => write!(f, "port range length mismatch"),
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pest(e) => write!(f, "pest error: {}", e),
            Self::Addr(e) => std::fmt::Display::fmt(e, f),
            Self::Num(e) => std::fmt::Display::fmt(e, f),
            Self::Invalid(e) => write!(f, "invalid: {}", e),
        }
    }
}

impl std::error::Error for Error {}

fn parse_action(pair: Pair<'_, Rule>) -> filter::Action {
    assert_eq!(pair.as_rule(), Rule::action);
    match pair.into_inner().next().unwrap().as_rule() {
        Rule::pass => filter::Action::Pass,
        Rule::drop => filter::Action::Drop,
        Rule::dropreset => filter::Action::DropReset,
        _ => unreachable!(),
    }
}

fn parse_direction(pair: Pair<'_, Rule>) -> filter::Direction {
    assert_eq!(pair.as_rule(), Rule::direction);
    match pair.into_inner().next().unwrap().as_rule() {
        Rule::incoming => filter::Direction::Incoming,
        Rule::outgoing => filter::Direction::Outgoing,
        _ => unreachable!(),
    }
}

fn parse_quick(pair: Pair<'_, Rule>) -> bool {
    assert_eq!(pair.as_rule(), Rule::quick);
    pair.as_str() == "quick"
}

fn parse_proto(pair: Pair<'_, Rule>) -> filter::SocketProtocol {
    assert_eq!(pair.as_rule(), Rule::proto);
    match pair.into_inner().next() {
        Some(pair) => match pair.as_rule() {
            Rule::tcp => filter::SocketProtocol::Tcp,
            Rule::udp => filter::SocketProtocol::Udp,
            Rule::icmp => filter::SocketProtocol::Icmp,
            _ => unreachable!(),
        },
        None => filter::SocketProtocol::Any,
    }
}

fn parse_src(
    pair: Pair<'_, Rule>,
) -> Result<(Option<Box<net::Subnet>>, bool, filter::PortRange), Error> {
    assert_eq!(pair.as_rule(), Rule::src);
    parse_src_or_dst(pair)
}

fn parse_dst(
    pair: Pair<'_, Rule>,
) -> Result<(Option<Box<net::Subnet>>, bool, filter::PortRange), Error> {
    assert_eq!(pair.as_rule(), Rule::dst);
    parse_src_or_dst(pair)
}

fn parse_src_or_dst(
    pair: Pair<'_, Rule>,
) -> Result<(Option<Box<net::Subnet>>, bool, filter::PortRange), Error> {
    let mut inner = pair.into_inner();
    match inner.next() {
        Some(pair) => match pair.as_rule() {
            Rule::invertible_subnet => {
                let (subnet, invert_match) = parse_invertible_subnet(pair)?;
                let port = match inner.next() {
                    Some(pair) => parse_port_range(pair)?,
                    None => filter::PortRange { start: 0, end: 0 },
                };
                Ok((Some(Box::new(subnet)), invert_match, port))
            }
            Rule::port_range => Ok((None, false, parse_port_range(pair)?)),
            _ => unreachable!(),
        },
        None => Ok((None, false, filter::PortRange { start: 0, end: 0 })),
    }
}

fn parse_invertible_subnet(pair: Pair<'_, Rule>) -> Result<(net::Subnet, bool), Error> {
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

fn parse_subnet(pair: Pair<'_, Rule>) -> Result<net::Subnet, Error> {
    assert_eq!(pair.as_rule(), Rule::subnet);
    let mut inner = pair.into_inner();
    let addr = parse_ipaddr(inner.next().unwrap())?;
    let prefix_len = parse_prefix_len(inner.next().unwrap())?;

    Ok(net::Subnet { addr: addr, prefix_len: prefix_len })
}

fn parse_ipaddr(pair: Pair<'_, Rule>) -> Result<net::IpAddress, Error> {
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

fn parse_prefix_len(pair: Pair<'_, Rule>) -> Result<u8, Error> {
    assert_eq!(pair.as_rule(), Rule::prefix_len);
    pair.as_str().parse::<u8>().map_err(Error::Num)
}

fn parse_port_range(pair: Pair<'_, Rule>) -> Result<filter::PortRange, Error> {
    assert_eq!(pair.as_rule(), Rule::port_range);
    let mut inner = pair.into_inner();
    let pair = inner.next().unwrap();
    match pair.as_rule() {
        Rule::port => {
            let port_num = parse_port_num(inner.next().unwrap())?;
            Ok(filter::PortRange { start: port_num, end: port_num })
        }
        Rule::range => {
            let port_start = parse_port_num(inner.next().unwrap())?;
            let port_end = parse_port_num(inner.next().unwrap())?;
            Ok(filter::PortRange { start: port_start, end: port_end })
        }
        _ => unreachable!(),
    }
}

fn parse_port_num(pair: Pair<'_, Rule>) -> Result<u16, Error> {
    pair.as_str().parse::<u16>().map_err(Error::Num)
}

fn parse_log(pair: Pair<'_, Rule>) -> bool {
    assert_eq!(pair.as_rule(), Rule::log);
    pair.as_str() == "log"
}

fn parse_state(pair: Pair<'_, Rule>) -> bool {
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

fn parse_rule(pair: Pair<'_, Rule>) -> Result<filter::Rule, Error> {
    assert_eq!(pair.as_rule(), Rule::rule);
    let mut pairs = pair.into_inner();

    let action = parse_action(pairs.next().unwrap());
    let direction = parse_direction(pairs.next().unwrap());
    let quick = parse_quick(pairs.next().unwrap());
    let proto = parse_proto(pairs.next().unwrap());
    let (src_subnet, src_subnet_invert_match, src_port_range) = parse_src(pairs.next().unwrap())?;
    let (dst_subnet, dst_subnet_invert_match, dst_port_range) = parse_dst(pairs.next().unwrap())?;
    let log = parse_log(pairs.next().unwrap());
    let keep_state = parse_state(pairs.next().unwrap());

    Ok(filter::Rule {
        action: action,
        direction: direction,
        quick: quick,
        proto: proto,
        src_subnet: src_subnet,
        src_subnet_invert_match: src_subnet_invert_match,
        src_port_range: src_port_range,
        dst_subnet: dst_subnet,
        dst_subnet_invert_match: dst_subnet_invert_match,
        dst_port_range: dst_port_range,
        nic: 0, // TODO: Support NICID (currently always 0 (= any))
        log: log,
        keep_state: keep_state,
    })
}

fn parse_nat(pair: Pair<'_, Rule>) -> Result<filter::Nat, Error> {
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

fn parse_rdr(pair: Pair<'_, Rule>) -> Result<filter::Rdr, Error> {
    assert_eq!(pair.as_rule(), Rule::rdr);
    let mut pairs = pair.into_inner();

    let proto = parse_proto(pairs.next().unwrap());
    let dst_addr = parse_ipaddr(pairs.next().unwrap())?;
    let dst_port_range = parse_port_range(pairs.next().unwrap())?;
    let new_dst_addr = parse_ipaddr(pairs.next().unwrap())?;
    let new_dst_port_range = parse_port_range(pairs.next().unwrap())?;

    Ok(filter::Rdr {
        proto: proto,
        dst_addr: dst_addr,
        dst_port_range: dst_port_range,
        new_dst_addr: new_dst_addr,
        new_dst_port_range: new_dst_port_range,
        nic: 0, // TODO: Support NICID.
    })
}

fn validate_port_range(range: &filter::PortRange) -> Result<(), Error> {
    if (range.start == 0 && range.end != 0) || range.start > range.end {
        return Err(Error::Invalid(InvalidReason::InvalidPortRange));
    }
    Ok(())
}

fn port_range_length(range: &filter::PortRange) -> Result<u16, Error> {
    let () = validate_port_range(&range)?;
    Ok(range.end - range.start)
}

fn ip_version_eq(left: &net::IpAddress, right: &net::IpAddress) -> bool {
    match (left, right) {
        (net::IpAddress::Ipv4(_), net::IpAddress::Ipv4(_))
        | (net::IpAddress::Ipv6(_), net::IpAddress::Ipv6(_)) => true,
        (net::IpAddress::Ipv4(_), net::IpAddress::Ipv6(_))
        | (net::IpAddress::Ipv6(_), net::IpAddress::Ipv4(_)) => false,
    }
}

fn validate_rule(rule: &filter::Rule) -> Result<(), Error> {
    if let (Some(src_subnet), Some(dst_subnet)) = (&rule.src_subnet, &rule.dst_subnet) {
        if !ip_version_eq(&src_subnet.addr, &dst_subnet.addr) {
            return Err(Error::Invalid(InvalidReason::MixedIPVersions));
        }
    }
    let () = validate_port_range(&rule.src_port_range)?;
    let () = validate_port_range(&rule.dst_port_range)?;

    Ok(())
}

fn validate_nat(nat: &filter::Nat) -> Result<(), Error> {
    if !ip_version_eq(&nat.src_subnet.addr, &nat.new_src_addr) {
        return Err(Error::Invalid(InvalidReason::MixedIPVersions));
    }

    Ok(())
}

fn validate_rdr(rdr: &filter::Rdr) -> Result<(), Error> {
    if !ip_version_eq(&rdr.dst_addr, &rdr.new_dst_addr) {
        return Err(Error::Invalid(InvalidReason::MixedIPVersions));
    }
    if port_range_length(&rdr.dst_port_range)? != port_range_length(&rdr.new_dst_port_range)? {
        return Err(Error::Invalid(InvalidReason::PortRangeLengthMismatch));
    }

    Ok(())
}

pub fn parse_str_to_rules(line: &str) -> Result<Vec<filter::Rule>, Error> {
    let mut pairs = FilterRuleParser::parse(Rule::rules, &line).map_err(Error::Pest)?;
    let mut rules = Vec::new();
    for filter_rule in pairs.next().unwrap().into_inner() {
        match filter_rule.as_rule() {
            Rule::rule => {
                let rule = parse_rule(filter_rule)?;
                let () = validate_rule(&rule)?;
                rules.push(rule);
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
                let nat = parse_nat(filter_rule)?;
                let () = validate_nat(&nat)?;
                nat_rules.push(nat);
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
                let rdr = parse_rdr(filter_rule)?;
                let () = validate_rdr(&rdr)?;
                rdr_rules.push(rdr);
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

    #[test]
    fn test_rule_with_proto_any() {
        assert_eq!(
            parse_str_to_rules("pass in;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Any,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 0, end: 0 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_proto_tcp() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 0, end: 0 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_multiple_rules() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp; drop out proto udp;"),
            Ok(vec![
                filter::Rule {
                    action: filter::Action::Pass,
                    direction: filter::Direction::Incoming,
                    quick: false,
                    proto: filter::SocketProtocol::Tcp,
                    src_subnet: None,
                    src_subnet_invert_match: false,
                    src_port_range: filter::PortRange { start: 0, end: 0 },
                    dst_subnet: None,
                    dst_subnet_invert_match: false,
                    dst_port_range: filter::PortRange { start: 0, end: 0 },
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
                    src_port_range: filter::PortRange { start: 0, end: 0 },
                    dst_subnet: None,
                    dst_subnet_invert_match: false,
                    dst_port_range: filter::PortRange { start: 0, end: 0 },
                    nic: 0,
                    log: false,
                    keep_state: true,
                },
            ])
        );
    }

    #[test]
    fn test_rule_with_from_v4_address() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from 1.2.3.4/24;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 24,
                })),
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 0, end: 0 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_from_port() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from port 10000;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 10000, end: 10000 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_from_range() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from range 10000:10010;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 10000, end: 10010 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_from_invalid_range_1() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from range 0:5;"),
            Err(Error::Invalid(InvalidReason::InvalidPortRange))
        );
    }

    #[test]
    fn test_rule_with_from_invalid_range_2() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from range 10005:10000;"),
            Err(Error::Invalid(InvalidReason::InvalidPortRange))
        );
    }

    #[test]
    fn test_rule_with_from_v4_address_port() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from 1.2.3.4/24 port 10000;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 24,
                })),
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 10000, end: 10000 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_from_not_v4_address_port() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from !1.2.3.4/24 port 10000;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                    prefix_len: 24,
                })),
                src_subnet_invert_match: true,
                src_port_range: filter::PortRange { start: 10000, end: 10000 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_from_v6_address_port() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp from 1234:5678::/32 port 10000;"),
            Ok(vec![filter::Rule {
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
                src_port_range: filter::PortRange { start: 10000, end: 10000 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_to_v6_address_port() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp to 1234:5678::/32 port 10000;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 0, end: 0 },
                dst_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    }),
                    prefix_len: 32,
                })),
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 10000, end: 10000 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_from_v6_address_port_to_v4_address_port() {
        assert_eq!(
            parse_str_to_rules(
                "pass in proto tcp from 1234:5678::/32 port 10000 to 1.2.3.4/8 port 1000;"
            ),
            Err(Error::Invalid(InvalidReason::MixedIPVersions))
        );
    }

    #[test]
    fn test_rule_with_from_v6_address_port_to_v6_address_port() {
        assert_eq!(
            parse_str_to_rules(
                "pass in proto tcp from 1234:5678::/32 port 10000 to 2345:6789::/32 port 1000;"
            ),
            Ok(vec![filter::Rule {
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
                src_port_range: filter::PortRange { start: 10000, end: 10000 },
                dst_subnet: Some(Box::new(net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0x23, 0x45, 0x67, 0x89, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    }),
                    prefix_len: 32,
                })),
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 1000, end: 1000 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_rule_with_log_no_state() {
        assert_eq!(
            parse_str_to_rules("pass in proto tcp log no state;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: false,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 0, end: 0 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: true,
                keep_state: false,
            }])
        );
    }

    #[test]
    fn test_rule_with_keep_state() {
        assert_eq!(
            parse_str_to_rules("pass in quick proto tcp keep state;"),
            Ok(vec![filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                quick: true,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port_range: filter::PortRange { start: 0, end: 0 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port_range: filter::PortRange { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: true,
            }])
        );
    }

    #[test]
    fn test_nat_rule_with_from_v4_subnet_to_v4_address() {
        assert_eq!(
            parse_str_to_nat_rules("nat from 1.2.3.0/24 -> from 192.168.1.1;"),
            Ok(vec![filter::Nat {
                proto: filter::SocketProtocol::Any,
                src_subnet: net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 0] }),
                    prefix_len: 24,
                },
                new_src_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                nic: 0,
            }])
        );
    }

    #[test]
    fn test_nat_rule_with_proto_tcp_from_v4_subnet_to_v4_address() {
        assert_eq!(
            parse_str_to_nat_rules("nat proto tcp from 1.2.3.0/24 -> from 192.168.1.1;"),
            Ok(vec![filter::Nat {
                proto: filter::SocketProtocol::Tcp,
                src_subnet: net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 0] }),
                    prefix_len: 24,
                },
                new_src_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                nic: 0,
            }])
        );
    }

    #[test]
    fn test_rdr_rule_with_to_v4_address_port_to_v4_address_port() {
        assert_eq!(
            parse_str_to_rdr_rules("rdr to 1.2.3.4 port 10000 -> to 192.168.1.1 port 20000;"),
            Ok(vec![filter::Rdr {
                proto: filter::SocketProtocol::Any,
                dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                dst_port_range: filter::PortRange { start: 10000, end: 10000 },
                new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                new_dst_port_range: filter::PortRange { start: 20000, end: 20000 },
                nic: 0,
            }])
        );
    }

    #[test]
    fn test_rdr_rule_with_proto_tcp_to_v4_address_port_to_v4_address_port() {
        assert_eq!(
            parse_str_to_rdr_rules(
                "rdr proto tcp to 1.2.3.4 port 10000 -> to 192.168.1.1 port 20000;"
            ),
            Ok(vec![filter::Rdr {
                proto: filter::SocketProtocol::Tcp,
                dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                dst_port_range: filter::PortRange { start: 10000, end: 10000 },
                new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                new_dst_port_range: filter::PortRange { start: 20000, end: 20000 },
                nic: 0,
            }])
        );
    }

    #[test]
    fn test_rdr_rule_with_to_v4_address_range_to_v4_address_range() {
        assert_eq!(
            parse_str_to_rdr_rules(
                "rdr proto tcp to 1.2.3.4 range 10000:10005 -> to 192.168.1.1 range 20000:20005;"
            ),
            Ok(vec![filter::Rdr {
                proto: filter::SocketProtocol::Tcp,
                dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                dst_port_range: filter::PortRange { start: 10000, end: 10005 },
                new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 1, 1] }),
                new_dst_port_range: filter::PortRange { start: 20000, end: 20005 },
                nic: 0,
            }])
        );
    }

    #[test]
    fn test_rdr_rule_with_to_v4_address_range_to_v4_address_invalid_range() {
        assert_eq!(
            parse_str_to_rdr_rules(
                "rdr proto tcp to 1.2.3.4 range 10000:10005 -> to 192.168.1.1 range 0:5;"
            ),
            Err(Error::Invalid(InvalidReason::InvalidPortRange))
        );
    }

    #[test]
    fn test_rdr_rule_with_to_v4_address_range_to_v4_address_port_range_length_mismatch() {
        assert_eq!(
            parse_str_to_rdr_rules(
                "rdr proto tcp to 1.2.3.4 range 10000:10005 -> to 192.168.1.1 range 20000:20003;"
            ),
            Err(Error::Invalid(InvalidReason::PortRangeLengthMismatch))
        );
    }

    #[test]
    fn test_rdr_rule_with_to_v4_address_range_to_v6_address_range() {
        assert_eq!(
            parse_str_to_rdr_rules(
                "rdr proto tcp to 1.2.3.4 range 10000:10005 -> to 1234:5678:: range 20000:20005;"
            ),
            Err(Error::Invalid(InvalidReason::MixedIPVersions))
        );
    }

    #[test]
    fn test_rdr_rule_with_to_v6_address_range_to_v6_address_range() {
        assert_eq!(
            parse_str_to_rdr_rules(
                "rdr proto tcp to 1234:5678:: range 10000:10005 -> to 2345:6789:: range 20000:20005;"
            ),
            Ok(vec![filter::Rdr {
                proto: filter::SocketProtocol::Tcp,
                dst_addr: net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: [0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                }),
                dst_port_range: filter::PortRange {
                    start: 10000,
                    end: 10005,
                },
                new_dst_addr: net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: [0x23, 0x45, 0x67, 0x89, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                }),
                new_dst_port_range: filter::PortRange {
                    start: 20000,
                    end: 20005,
                },
                nic: 0,
            }])
        );
    }
}

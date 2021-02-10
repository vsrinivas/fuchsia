# Netfilter Library

## Text Rule Parser API
  * parse regular rules
    ```
    pub parse_str_to_rules(line: &str) -> Result<Vec<fuchsia_net_filter::Rule>, pest::error::Error<Rule>>
    ```
  * parse NAT rules
    ```
    pub parse_str_to_nat_rules(line: &str) -> Result<Vec<fuchsia_net_filter::Nat>, pest::error::Error<Rule>>
    ```
  * parse RDR rules
    ```
    pub parse_str_to_rdr_rules(line: &str) -> Result<Vec<fuchsia_net_filter::Rdr>, pest::error::Error<Rule>>
    ```

## Regular Rule Syntax
  ```
  action direction ["proto" proto]
     ["from" [["!"]src_subnet] [src_port]]
     ["to" [["!"]dst_subnet] [dst_port]] [log] [state] ";"
  ```

  * action

    "pass", "drop", or "dropreset".

  * direction

    "in" or "out".

  * proto (optional)

    "tcp", "udp", or "icmp".

  * src\_subnet, dst\_subnet (optional)

    IP address and netmask in CIDR Notation.
    If this is ommitted, any address can match.

  * src\_port, dst\_port (optional)

    "port" port-number, or
    "range" start-port-number":"end-port-number.

    If this is ommited, any port can match.

  * log (optional)

    Enable logging. Logging is disabled by default.

  * state (optional)

    "keep state" or "no state" (default).

### Examples

  * "pass in proto tcp from 2607:f8b0:4005:80b::/64 port 10000 to 192.168.42.0/24 port 1000;"

    ```
    &[
        filter::Rule {
            action: filter::Action::Pass,
            direction: filter::Direction::Incoming,
            proto: net::SocketProtocol::Tcp,
            src_subnet: Some(Box::new(net::Subnet{
                addr: net::IpAddress::Ipv6(net::Ipv6Address{
                    addr: [0x26, 0x07, 0xf8, 0xb0, 0x40, 0x05, 0x08, 0x0b, 0, 0, 0, 0, 0, 0, 0, 0]
                }),
                prefix_len: 64,
            })),
            src_subnet_invert_match: false,
            src_port: filter::Port { start: 10000, end: 10000 },
            dst_subnet: Some(Box::new(net::Subnet{
                addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [192, 168, 42, 0] }),
                prefix_len: 24,
            })),
            dst_subnet_invert_match: false,
            dst_port: filter::Port { start: 1000, end: 1000 },
            nic: 0,
            log: false,
            keep_state: false,
        },
    ]
    ```

  * "pass in proto tcp from range 10000:10010;"

    ```
    &[
        filter::Rule {
                action: filter::Action::Pass,
                direction: filter::Direction::Incoming,
                proto: filter::SocketProtocol::Tcp,
                src_subnet: None,
                src_subnet_invert_match: false,
                src_port: filter::Port { start: 10000, end: 10010 },
                dst_subnet: None,
                dst_subnet_invert_match: false,
                dst_port: filter::Port { start: 0, end: 0 },
                nic: 0,
                log: false,
                keep_state: false,
        }
    ]
    ```

## NAT Rule Syntax
  ```
  "nat" ["proto" proto] "from" subnet "->" "from" ipaddr ";"
  ```

  * proto (optional)

    "tcp", "udp", or "icmp".

  * subnet

    IP address and netmask in CIDR Notation.

  * ipaddr

    IP address.

### Examples

  * "nat from 192.168.42.0/24 -> from 10.0.0.1;"
    ```
    &[
        filter::Nat {
            proto: filter::SocketProtocol::Any,
            src_subnet: net::Subnet{
                addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [192, 168, 42, 0] }),
                prefix_len: 24,
            },
            new_src_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [10, 0, 0, 1] }),
            nic: 0,
        },
    ]
    ```

## RDR Rule Syntax
  ```
  "rdr" ["proto" proto] "to" ipaddr dst_port "->" "to" ipaddr dst_port ";"
  ```

  * proto (optional)

    "tcp", "udp", or "icmp".

  * ipaddr

    IP address.

  * dst\_port

    "port" port-number, or
    "range" start-port-number":"end-port-number

### Examples

  * "rdr proto tcp to 10.0.0.1 port 10000 -> to 192.168.42.1 port 10000;"
    ```
    &[
        filter::Rdr {
            proto: net::SocketProtocol::Tcp,
            dst_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [10, 0, 0, 1] }),
            dst_port_range: filter::Port { start: 10000, end: 10000 },
            new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [192, 168, 42, 1] }),
            new_dst_port_offset: 10000,
            nic: 0,
        },
    ]
    ```

  * "rdr proto tcp to 10.0.0.1 range 10000:10005 -> to 192.168.42.1 range 20000:20005;"
    ```
    &[
        filter::Rdr {
            proto: net::SocketProtocol::Tcp,
            dst_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [10, 0, 0, 1] }),
            dst_port_range: filter::Port { start: 10000, end: 10005 },
            new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [192, 168, 42, 1] }),
            new_dst_port_range: filter::Port { start: 20000, end: 20005 },
            nic: 0,
        },
    ]
    ```

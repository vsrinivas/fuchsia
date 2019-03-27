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
  action direction ["quick"] "proto" proto
     ["from" [["!"]src_subnet] ["port" src_port]]
     ["to" [["!"]dst_subnet] ["port" dst_port]] [log] [state] ";"
  ```

  * action

    "pass", "drop", or "dropreset".

  * direction

    "in" or "out".

  * quick (optional)

    The rule is selected immediately. Usually a rule is selected only
	if that is the last matched rule in the set of rules.

  * proto

    "tcp", "udp", or "icmp".

  * src_subnet, dst_subnet (optional)

    IP address and netmask in CIDR Notation.
    If this is ommitted, any address can match.

  * src_port, dst_port (optional)

    Port number. If this is ommited, any port can match.

  * log (optional)

    Enable logging. Logging is disabled by default.

  * state (optional)

    "keep state" (default) or "no state".

### Examples

  * "pass in proto tcp from 1234:5678::/32 port 10000 to 1.2.3.4/8 port 1000;"

    ```
    &[
        filter::Rule {
            action: filter::Action::Pass,
            direction: filter::Direction::Incoming,
            quick: false,
            proto: net::SocketProtocol::Tcp,
            src_subnet: Some(Box::new(net::Subnet{
                addr: net::IpAddress::Ipv6(net::Ipv6Address{
                    addr: [0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                }),
                prefix_len: 32,
            })),
            src_subnet_invert_match: false,
            src_port: 10000,
            dst_subnet: Some(Box::new(net::Subnet{
                addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [1, 2, 3, 4] }),
                prefix_len: 8,
            })),
            dst_subnet_invert_match: false,
            dst_port: 1000,
            nic: 0,
            log: false,
            keep_state: true,
        },
    ]
    ```

## NAT Rule Syntax
  ```
  "nat" "proto" proto "from" subnet "->" "from" ipaddr ";"
  ```

  * proto

    "tcp", "udp", or "icmp".

  * subnet

    IP address and netmask in CIDR Notation.

  * ipaddr

    IP address.

### Examples

  * "nat proto tcp from 1.2.3.0/24 -> from 192.168.1.1;"
    ```
    &[
        filter::Nat {
            proto: net::SocketProtocol::Tcp,
            src_subnet: net::Subnet{
                addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [1, 2, 3, 0] }),
                prefix_len: 24,
            },
            new_src_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [192, 168, 1, 1] }),
            nic: 0,
        },
    ]
    ```

## RDR Rule Syntax
  ```
  "rdr" "proto" proto "to" ipaddr "port" port "->" "to" ipaddr "port" port ";"
  ```

  * proto

    "tcp", "udp", or "icmp".

  * ipaddr

    IP address.

  * port

    port number.

### Examples

  * "rdr proto tcp to 1.2.3.4 port 10000 -> to 192.168.1.1 port 20000;"
    ```
    &[
        filter::Rdr {
            proto: net::SocketProtocol::Tcp,
            dst_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [1, 2, 3, 4] }),
            dst_port: 10000,
            new_dst_addr: net::IpAddress::Ipv4(net::Ipv4Address{ addr: [192, 168, 1, 1] }),
            new_dst_port: 20000,
            nic: 0,
        },
    ]
    ```


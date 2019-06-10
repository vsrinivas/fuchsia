[![Crates.io](https://img.shields.io/crates/v/eui48.svg)](https://crates.io/crates/eui48)
[![docs.rs](https://docs.rs/eui48/badge.svg)](https://docs.rs/eui48)
[![Build Status](https://ci.appveyor.com/api/projects/status/github/abaumhauer/eui48?branch=master&svg=true)](https://ci.appveyor.com/project/abaumhauer/eui48/branch/master)
[![Build Status](https://gitlab.com/abaumhauer/eui48/badges/master/build.svg)](https://gitlab.com/abaumhauer/eui48/commits/master)
[![Build Status](https://travis-ci.org/abaumhauer/eui48.svg?branch=master)](https://travis-ci.org/abaumhauer/eui48)
[![Coverage Status](https://codecov.io/gh/abaumhauer/eui48/branch/master/graph/badge.svg)](https://codecov.io/gh/abaumhauer/eui48)

eui48
====

A Rust library to represent and parse IEEE EUI-48 also known as MAC-48 media access control addresses. The IEEE claims trademarks on the names EUI-48 and EUI-64, in which EUI is an abbreviation for Extended Unique Identifier.

## Usage

Add this to your `Cargo.toml`:

```toml
[dependencies]

eui48 = "0.4.6"
```

and this to your crate root:

```rust
extern crate eui48;
```

## Examples

To create a new MAC address and print it out in canonical form:

```rust
extern crate eui48;
use eui48::{MacAddress, Eui48};

fn main() {
	let eui: Eui48 = [ 0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF ];
	let mac = MacAddress::new( eui );

	println!("{}", mac.to_canonical());
	println!("{}", mac.to_hex_string());
	println!("{}", mac.to_dot_string());
	println!("{}", mac.to_hexadecimal());
	println!("{}", mac.to_interfaceid());
	println!("{}", mac.to_link_local());

	let mac = MacAddress::parse_str( "01-02-03-0A-0b-0f" ).expect("Parse error {}");
	let mac = MacAddress::parse_str( "01:02:03:0A:0b:0f" ).expect("Parse error {}");
	let mac = MacAddress::parse_str( "0102.030A.0b0f" ).expect("Parse error {}");
	let mac = MacAddress::parse_str( "0x1234567890ab" ).expect("Parse error {}");
}
```

## References
[Wikipedia: MAC address](https://en.wikipedia.org/wiki/MAC_address)

## Authors
- 0.1 Andrew Baumhauer - Initial design
- 0.2 rlcomstock3 - Added support for btree keys
- 0.3 Michal 'vorner' Vaner <vorner+github@vorner.cz> - Serde 1.0 support
- 0.3.1 Michal 'vorner' Vaner <vorner+github@vorner.cz> - Derive useful traits
- 0.4.0 Rainer Stademann - Define ABI as repr(C)
- 0.4.1 Andrew Baumhauer - Add IPv6 Interface ID and Link Local conversions
- 0.4.2 Andrew Baumhauer / Eric Clone - Bug fix in is_local() and is_unicast() functions
- 0.4.3 Andrew Baumhauer - Update travis-ci, appveyor, codecov
- 0.4.4 Andrew Baumhauer - Update documentation
- 0.4.5 Andrew Baumhauer - Improve code coverage and tests
- 0.4.6 Jiwoong Lee - Add to_array() for compatibility, add feature disp_hexstring

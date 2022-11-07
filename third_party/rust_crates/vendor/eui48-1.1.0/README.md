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

eui48 = "1.0.1"
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

## Notes
* The default display format is cannonical form `01-02-03-04-05-06` unless a compile time feature `disp_hexstring` is enabled, then the default format is of the form `01:02:03:04:05:06`.

Version 1.0.0 and above allows a more flexible parsing of MAC address strings, compliments of Stan Drozd:
* Enables the library's caller to parse the MACs that don't follow fixed-length MAC address convention (I'm looking at you, ebtables!). In general, the parsing function tries harder to interpret a given string than before.
* Rewrite parse_str to use a regex and be more lenient (now it permits one-off string chopping errors and mixed delimiters are accepted as long as we manage to read 6 bytes)
* Exchange the InvalidCharacter error enum value for InvalidByteCount - InvalidCharacter is no longer supported. See versions >=0.5.0 and < 1.0.0 if you need legacy behavior.

## Serialization
When using `serde` to serialize a MAC address the address is stored as a formatted string. This fits well for text-based protocols like JSON but creates overhead for binary serialization. The overhead gets even bigger when the string is deserialized again, as a full-grown parser is needed instead of reading raw bytes. To reduce this overhead use the `serde_bytes` feature when serializing and deserializing MAC addresses to binary protocols. 

NOTE: `serde_bytes` and `serde_json` are mutually exclusive!

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
- 0.4.7 Adam Reichold - WASM updates
- 0.4.8 @kamek-pf - respect disp_hexstring flag
- 0.4.9 Sebastian Dietze - New const added
- 0.5.0 Andrew Baumhauer - cleanup, update versions, fmt, merge PRs, update unit tests
- 0.5.1 jrb0001 - Fixed incorrect IPv6 to_link_local for Link-Scoped Unicast
- 1.0.0 Stan Drozd, @rlcomstock3, and Andrew Baumhauer - merged all forks and improvements back to this repo
- 1.0.1 jrb0001 - Fixed incorrect IPv6 to_link_local for Link-Scoped Unicast
- 1.1.0 Felix Schreiner - binary serialization optimization

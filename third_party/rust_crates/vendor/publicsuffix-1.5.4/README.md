# Robust domain name parsing and RFC compliant email address validation

[![Build Status](https://travis-ci.org/rushmorem/publicsuffix.svg?branch=master)](https://travis-ci.org/rushmorem/publicsuffix) [![Latest Version](https://img.shields.io/crates/v/publicsuffix.svg)](https://crates.io/crates/publicsuffix) [![Docs](https://docs.rs/publicsuffix/badge.svg)](https://docs.rs/publicsuffix)

This library uses Mozilla's [Public Suffix List](https://publicsuffix.org) to reliably parse domain names and email addresses in [Rust](https://www.rust-lang.org). Though parsing domain names is it's primary goal, it also fully exposes the list allowing you to use convenient methods like `list.all()` to get all known domain extensions or `list.icann()` to get only ICANN extensions.

If all you need is to check whether a domain is syntactically correct and do not need to utilise the list you can just use `Domain::has_valid_syntax` method. This method will reliably tell you if a domain has valid syntax whether or not it is an internationalised domain name (IDN). It also checks the length restrictions for each label, total number of labels and full length of domain name.

This crate doesn't cache the public suffix list for you. If you want to use this crate in a long running application and want to make use of the public suffix list, I highly recommend you use the [psl](https://github.com/rushmorem/psl) crate which does this for you.

## Setting Up

Add this crate to your `Cargo.toml`:

```toml
[dependencies.publicsuffix]
version = "1.5"

# This crate exposes the methods `List::fetch` and `List::from_url` as a
# feature named "remote_list". This feature is on by default. If you have
# the public suffix list on your local filesystem or you would like
# to fetch this list on your own you can disable this feature and build
# the list using `List::from_path` or `List::from_reader` respectively.
#
# To disable, uncomment the line below:
# default-features = false
```

## Examples

```rust
extern crate publicsuffix;

use publicsuffix::List;

// Fetch the list from the official URL,
let list = List::fetch()?;

// from your own URL
let list = List::from_url("https://example.com/path/to/public_suffix_list.dat")?;

// or from a local file. You can download the list from
// "https://publicsuffix.org/list/public_suffix_list.dat".
let list = List::from_path("/path/to/public_suffix_list.dat")?;

// Using the list you can find out the root domain
// or extension of any given domain name
let domain = list.parse_domain("www.example.com")?;
assert_eq!(domain.root(), Some("example.com"));
assert_eq!(domain.suffix(), Some("com"));

let domain = list.parse_domain("www.食狮.中国")?;
assert_eq!(domain.root(), Some("食狮.中国"));
assert_eq!(domain.suffix(), Some("中国"));

let domain = list.parse_domain("www.xn--85x722f.xn--55qx5d.cn")?;
assert_eq!(domain.root(), Some("xn--85x722f.xn--55qx5d.cn"));
assert_eq!(domain.suffix(), Some("xn--55qx5d.cn"));

let domain = list.parse_domain("a.b.example.uk.com")?;
assert_eq!(domain.root(), Some("example.uk.com"));
assert_eq!(domain.suffix(), Some("uk.com"));

let name = list.parse_dns_name("_tcp.example.com.")?;
assert_eq!(name.domain().and_then(|domain| domain.root()), Some("example.com"));
assert_eq!(name.domain().and_then(|domain| domain.suffix()), Some("com"));

// You can also find out if this is an ICANN domain
assert!(!domain.is_icann());

// or a private one
assert!(domain.is_private());

// In any case if the domain's suffix is in the list
// then this is definately a registrable domain name
assert!(domain.has_known_suffix());
```

## Use Cases

For those who work with domain names the use cases of this library are plenty. [publicsuffix.org/learn](https://publicsuffix.org/learn/) lists quite a few. For the sake of brevity, I'm not going to repeat them here. I work for a domain registrar so we make good use of this library. Here are some of the ways this library can be used:-

* Validating domain names. This one is probably obvious. If a [Domain::has_known_suffix](https://docs.rs/publicsuffix/*/publicsuffix/struct.Domain.html#method.has_known_suffix) you can be absolutely sure this is a valid domain name. A regular expression is simply not robust enough.
* Validating email addresses. You can utilise this library to validate email addresses in a robust and reliable manner before resorting to more expensive (DNS checks) or less convenient (sending confirmation emails) ways.
* Blacklisting or whitelisting domain names and email addresses. You can't just blindly do this without knowing the actual registrable domain name otherwise you risk being too restrictive or too lenient. Bad news either way...
* Extracting the registrable part of a domain name so you can check whether the domain is registered or not.
* Storing details about a domain name in a DBMS using the registrable part of a domain name as the primary key.
* Like my company, a registrar or similar organisation can draft their own list of domain extensions they support, following the same specs as the original list, and then use this library to check whether a requested domain name is actually supported.

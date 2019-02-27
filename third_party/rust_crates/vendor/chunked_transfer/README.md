# rust-chunked-transfer

[![Build Status](https://travis-ci.org/frewsxcv/rust-chunked-transfer.svg?branch=master)](https://travis-ci.org/frewsxcv/rust-chunked-transfer)
[![chunked\_transfer on Crates.io](https://meritbadge.herokuapp.com/chunked_transfer)](https://crates.io/crates/chunked\_transfer)

[Documentation](https://frewsxcv.github.io/rust-chunked-transfer/)

Encoder and decoder for HTTP chunked transfer coding. For more information about chunked transfer encoding:

* [RFC 7230 § 4.1](https://tools.ietf.org/html/rfc7230#section-4.1)
* [RFC 2616 § 3.6.1](http://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html#sec3.6.1) (deprecated)
* [Wikipedia: Chunked transfer encoding](https://en.wikipedia.org/wiki/Chunked_transfer_encoding)

## Example

### Decoding

```rust
use chunked_transfer::Decoder;
use std::io::Read;

let encoded = b"3\r\nhel\r\nb\r\nlo world!!!\r\n0\r\n\r\n";
let mut decoded = String::new();

let mut decoder = Decoder::new(encoded as &[u8]);
decoder.read_to_string(&mut decoded);

assert_eq!(decoded, "hello world!!!");
```

### Encoding

```rust
use chunked_transfer::Encoder;
use std::io::Write;

let mut decoded = "hello world";
let mut encoded: Vec<u8> = vec![];

{
    let mut encoder = Encoder::with_chunks_size(&mut encoded, 5);
    encoder.write_all(decoded.as_bytes());
}

assert_eq!(encoded, b"5\r\nhello\r\n5\r\n worl\r\n1\r\nd\r\n0\r\n\r\n");
```

## Authors

* [tomaka](https://github.com/tomaka)
* [frewsxcv](https://github.com/frewsxcv)

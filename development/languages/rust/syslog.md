# Syslog

This document explains how to use rust syslogger library.


## Cargo.toml dependency

```
fuchsia-syslog = "0.1"
```

### Initialization

Logger can only be initialized once.

#### Basic initialization

```rust
#[macro_use]
extern crate fuchsia_syslog as syslog;

fn main() {
    syslog::init().expect("should not fail");
}
```

#### Initialization with tags

```rust
#[macro_use]
extern crate fuchsia_syslog as syslog;

fn main() {
    syslog::init_with_tags(&["my_tags"]).expect("should not fail");
}
```

### Log messages

```rust
fx_log_info!("my msg: %d", 10);
fx_log_info!(tag: "tag", "my msg: %d", 10);

fx_log_err!("my msg: %d", 10);
fx_log_err!(tag: "tag", "my msg: %d", 10);

fx_log_warn!("my msg: %d", 10);
fx_log_warn!(tag: "tag", "my msg: %d", 10);

fx_vlog!(1, "my msg: %d", 10); // verbose logs
fx_vlog!(tag: "tag", 1, "my msg: %d", 10); // verbose logs
```

### Reference
[Rust APIs](https://fuchsia.googlesource.com/garnet/+/master/public/rust/crates/fuchsia-syslog/src/lib.rs)

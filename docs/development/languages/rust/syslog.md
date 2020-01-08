# Syslog

This document explains how to use rust syslogger library.


## BUILD.gn dependency

```gn
"//src/lib/syslog/rust:syslog"
```

### Initialization

Logger can only be initialized once.

#### Basic initialization

```rust
use fuchsia_syslog as syslog;

fn main() {
    syslog::init().expect("should not fail");
}
```

#### Initialization with tags

```rust
use fuchsia_syslog as syslog;

fn main() {
    syslog::init_with_tags(&["my_tags"]).expect("should not fail");
}
```

### Log messages

```rust
use fuchsia_syslog::{fx_log_info, fx_log_warn, fx_log_err, fx_vlog};
// alternate export for all the logging macros:
// use fuchsia_syslog::{macros::*}

fx_log_info!("my msg: {}", 10);
fx_log_info!(tag: "tag", "my msg: {}", 10);

fx_log_err!("my msg: {}", 10);
fx_log_err!(tag: "tag", "my msg: {}", 10);

fx_log_warn!("my msg: {}", 10);
fx_log_warn!(tag: "tag", "my msg: {}", 10);

fx_vlog!(1, "my msg: {}", 10); // verbose logs
fx_vlog!(tag: "tag", 1, "my msg: {}", 10); // verbose logs
```

This can also be used with rust [log crate](https://docs.rs/log/)

```rust
info!("my msg: {}", 10);
warn!("my msg: {}", 10);
error!("my msg: {}", 10);
```


### Reference
[Rust APIs](/src/lib/syslog/rust/src/lib.rs)

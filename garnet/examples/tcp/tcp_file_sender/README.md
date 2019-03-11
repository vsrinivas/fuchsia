# TCP file sender

This TCP file sender accepts inbound TCP connections and sends a single file to them before closing.

## Build

```
$ fx set x64 --monolith garnet/packages/examples/tcp
$ fx full-build
```

## Usage

To run the server on the fuchsia device, from fx shell:

```
run fuchsia-pkg://fuchsia.com/tcp_file_sender#meta/tcp_file_sender.cmx <FILE>
```

To retrieve the byte stream:
* `nc $(fx netaddr --fuchsia) 80 > out`

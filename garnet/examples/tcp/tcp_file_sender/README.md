# TCP file sender

This TCP file sender accepts inbound TCP connections and sends a single file to them before closing.

## Build

```
$ fx set core.x64 --with //garnet/examples/tcp/tcp_file_sender
$ fx build
```

## Usage

To run the server on the fuchsia device, from fx shell:

```
run fuchsia-pkg://fuchsia.com/tcp_file_sender#meta/tcp_file_sender.cmx <FILE>
```

To retrieve the byte stream:
* `nc $(fx get-device-addr) 80 > out`

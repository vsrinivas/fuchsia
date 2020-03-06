# ping3
> A ping tool that uses ICMP echo sockets instead of raw sockets.


```
$ ping3 --help
ping3 0.1.0
Sends ICMP echo requests to a host and displays relies.

If ping3 does not receive any reply packets at all, it will exit with code 1. If `count` and
`deadline` are both specified, and fewer than `count` replies are received by the time the
`deadline` has expired, it will also exit with code 1. On other errors it exits with code 2.
Otherwise it exits with code 0.

In other words, exit code 0 implies the host is alive. Code 1 implies the host is dead. Code 2 shows
the state of the host cannot be determined.

USAGE:
    ping3 [FLAGS] [OPTIONS] <remote>

FLAGS:
    -h, --help
            Prints help information

    -V, --version
            Prints version information

    -v, --verbose
            Enables detailed logging and tracing


OPTIONS:
    -c, --count <count>
            Number of ICMP echo requests to send before stopping.

    -w, --deadline <deadline>
            A timeout, in seconds, before exiting regardless of how many ICMP echo requests have
            been sent or how many ICMP echo replies have been received.
    -i, --interval <interval>
            Milliseconds to wait between sending ICMP echo requests. [default: 1000]

    -l, --local <local_addr>
            Source IP address of the ICMP echo requests.

    -s, --size <packet_size>
            Specifies the number of data bytes to be sent. The final size of the ICMP packet will be
            this value plus 8 bytes for the ICMP header. [default: 56]

ARGS:
    <remote>
            Destination IP address to send ICMP echo requests to.

```

## Compatibility

 - netstack2 (Go): [Backlogged](https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=47826)
 - netstack3 (Rust): Supported


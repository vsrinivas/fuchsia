# fx pretty_serial

pretty_serial is a host tool that beautifies the serial output of a Fuchsia device. This program
will use regex matching to parse all serial output piped in and will strip out extraneous metadata
(such as filenames, PID and TID). It will also colorize log lines based on severity.

## Usage

Pipe the FEMU serial output into `pretty-serial`:

```
fx emu | fx pretty-serial
```
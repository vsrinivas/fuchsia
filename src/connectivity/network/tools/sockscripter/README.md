# sockscripter tool

The `sockscripter` tool is a shell program that allows to quickly script a
succession of POSIX API socket commands. Each argument to the program represents
a POSIX API command. See the help text from the program for the currently
support APIs.

New APIs or options can easily be added by a developer. See the Developer Notes
section below for details.

## Building
The tool can be built for both Fuchsia and Linux.

Add `--with //src/connectivity/network/tools/sockscripter` to your `fx
set`. That will build `sockscripter` for both the host and fuchsia.

## Usage
On Fuchsia, the tool is available as a shell script via `fx shell sockscripter`.

As a convenience a source script `sockscripter.sh` is available that
installs two functions: `sockscripterl` to run the host binary and
`sockscripterf` to run the Fuchsia one.

### complete script

The `sockscripter.sh` script also provides a bash complete script and assigns
it to the `sockscripterl` and `sockscripterf` functions.

## Examples

Sending a multicast packet:
`sockscripter udp set-mcast-if4 192.168.1.166 sendto 224.0.0.120:2000`

Receiving that multicast packet:
`sockscripter udp bind any:2000 join4 224.0.0.120-192.168.1.99 recvfrom`

## Developer Notes

Adding support for new arguments is a simple two step process:

1. Implement a new command function in the `SockScripterClass` (e.g. `bool
Bind(char *arg)`).
2. Insert a new line in the 'struct Command kCommands[]' structure. You have to
   provide four things:
  - the name of the command as used in the command-line (e.g. `"bind"`)
  - a help string for an additional argument the command needs
    (e.g. `"<bind-ip>:<bind-port>"`), NULL if no additional argument is used.
  - a help text for this command, e.g. `"socket addr to bind to"`
  - a pointer to the member function from step 1.

# Using the debugger

## Current status

Currently the debugger isn't usable for normal debugging tasks.

These instructions are currently applicable to people actually working on the
debugger.

## Running remotely

The debugger does not run on the target system.

#### 1. Compile the test app

For testing, enable the test app. In `garnet/bin/debug_agent/BUILD.gn` set
the variable `include_test_app` at the top of the file to `true`.

Build normally.

#### 2. Boot with networking

Boot the target system with networing support. For
[QEMU support](https://fuchsia.googlesource.com/docs/+/HEAD/getting_started.md)
you may get some prompts for extra steps required:

```sh
fx run -N -u scripts/start-dhcp-server.sh
```

#### 3. Run the debug agent

You will also want to note the target's IP address (run `ifconfig` _on the
target_ to see this).

On the target system pick a port and run the debug agent:

```sh
debug_agent --port=2345
```

#### 4. Run the client and connect

Using the build directory from above, run the client:

```sh
out/x64/host_x64/zxdb
```

And then connect to the agent using the IP address for the target, and the
same port number you used when running the agent:

```
[zxdb] connect 192.168.3.20 2345
Connected successfully.
[zxdb]
```

## Super quick start

Run a program:

```
[zxdb] run /path/to/some/program
```

Attach to a program:

```
[zxdb] ps
...process tree...

[zxdb] attach 3452
```

Type "help" for more commands, there is an extensive built-in help system.

## How to use the test app

The test app starts and immediately issues a hardcoded debugger breakpoint.
Dynamic library load notifications are not hooked up yet, so you need to
run the `libs` command which will force discovery of them. Symbols won't work
until you do this.

```
[zxdb] connect 192.168.3.53 2345
Connecting (use "disconnect" to cancel)...
Connected successfully.

[zxdb] run /pkgfs/packages/debug_agent_tests/0/test/zxdb_test_app

[zxdb] libs
    Load address  Name
  0x173216aac000  libc++abi.so.1
  0x2c21c7af8000  libfdio.so
  0x4e60a3b75000  <vDSO>
  0x62585339a000  libc.so
  0x6826deca9000  libunwind.so.1
  0x6a272d06b000  libc++.so.2
  0x70dd4213b000

[zxdb] b PrintHello
Breakpoint 1 on Global, Enabled, stop=All, @ PrintHello

[zxdb] c
Thread 1 stopped at PrintHello() • zxdb_test_app.cc:26
  24  }
  25
▶ 26  void PrintHello() {
  27    const char msg[] = "Hello from zxdb_test_app!\n";
  28    zx_debug_write(msg, strlen(msg));

[zxdb] b 28
Breakpoint 2 on Global, Enabled, stop=All, @ zxdb_test_app.cc:28

[zxdb] c
Thread 1 stopped at PrintHello() • zxdb_test_app.cc:28
  26  void PrintHello() {
  27    const char msg[] = "Hello from zxdb_test_app!\n";
▶ 28    zx_debug_write(msg, strlen(msg));
  29
  30    // This code is here to test disassembly of FP instructions.

[zxdb] si
```


## Running the tests

There are tests for the debugger that run on the host. These are relavant
if you're working on the debugger client.

```sh
out/x64/host_x64/zxdb_tests
```

The debug agent tests are in
```
/pkgfs/packages/debug_agent_tests/0/test/debug_agent_tests
```

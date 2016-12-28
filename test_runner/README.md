test_runner is a TCP daemon for magenta. It accepts connections, reads shell
commands, executes them while streaming back the stdout and stderr back to the
client. It is meant to invoke programs and view their output remotely, primarily
for testing purpose.

Prerequisite:
- An instance of magenta running (qemu or otherwise), configured with network
  that host (Linux or Mac) can interface over.


Launch test_runner (remotely) using netruncmd on host (Linux or Mac):
```
$ out/build-magenta/tools/netruncmd magenta /system/apps/test_runner
```

(if /system/apps/test_runner doesn't exist on your image, you can always netcp
it)

Use `netcat` to connect to test_runner and run a command:
```
$ echo /system/test/lib_fidl_cpp_tests | netcat 192.168.3.53 8342
```

(your QEMU's network interface may be configured on a different IP. using
`ifconfig` to figure out what the inet address is.).

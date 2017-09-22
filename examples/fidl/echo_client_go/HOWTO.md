# How to run this program

To run this example, you need a server program registered in the environment.
Add either:
```
"echo::Echo": "echo_server_go",
```
or
```
"echo::Echo": "echo_server_cpp",
```
to [//application/src/bootstrap/services.config](https://fuchsia.googlesource.com/application/+/master/src/bootstrap/services.config).

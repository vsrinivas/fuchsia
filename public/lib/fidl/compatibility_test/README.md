# FIDL compatibility test

An integration test for compatability of different FIDL bindings.

The test runner is at `//topaz/bin/fidl_compatibility_test` and can be invoked
on device with:
```sh
run /pkgfs/packages/fidl_compatibility_test/0/test/fidl_compatibility_test
```

The basic logic is along the lines of:

```python
servers = ['go_server', 'cc_server', ...]

for proxy_name in servers:
  for server_name in servers:
    proxy = <start proxy with LaunchPad>
    struct = <construct complicated struct>
    struct.forward_to_server = server_name
    resp = proxy.EchoStruct(struct)
    assert_equal(struct, resp)
```

Servers should implement the service defined in
[compatibility_test_service.fidl](compatibility_test_service.fidl) with logic
along the lines of:

```python
def EchoStruct(Struct value, EchoStructCallback callback):
  if value.forward_to_server:
    other_server = <start server with LaunchPad>
    value.forward_to_server = ""  # prevent recursion
    other_server.EchoStruct(value, callback)
  else:
    callback(value)
```

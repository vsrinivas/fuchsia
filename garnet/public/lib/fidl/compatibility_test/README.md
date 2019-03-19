# FIDL compatibility test

An integration test for compatibility of different FIDL bindings.

The test runner is at `//garnet/bin/fidl_compatibility_test` and
`//topaz/bin/fidl_compatibility_test` and can be invoked on device with:
```sh
run /pkgfs/packages/fidl_compatibility_test/0/test/fidl_compatibility_test
```

The version in topaz tests more languages than the version in garnet.

The basic logic is along the lines of:

```python
servers = ['go_server', 'cc_server', ...]

for proxy_name in servers:
  for server_name in servers:
    proxy = <connect to proxy>
    struct = <construct complicated struct>
    resp = proxy.EchoStruct(struct, server_name)
    assert_equal(struct, resp)
```

Servers should implement the service defined in
[compatibility_test_service.fidl](compatibility_test_service.fidl) with logic
along the lines of:

```python
def EchoStruct(
    Struct value, string forward_to_server, EchoStructCallback callback):
  if value.forward_to_server:
    other_server = <start server with LaunchPad>
    # set forward_to_server to "" to prevent recursion
    other_server.EchoStruct(value, "", callback)
  else:
    callback(value)
```

The logic for `EchoStructNoRetVal()` is similar. Instead of waiting for a
response directly, the test waits to receive an `EchoEvent()`. And instead of
calling the client back directly, the server sends the `EchoEvent()`.

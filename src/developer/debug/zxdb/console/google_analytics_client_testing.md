# Testing the lite Google Analytics client (manually)
Compile `zxdb_google_analytics_client_manualtest`
```
fx set core.x64  --with '//src/developer/debug/zxdb/console:zxdb_google_analytics_client_manualtest(//build/toolchain:host_x64)'
_
```

Run `zxdb_google_analytics_client_manualtest`
```
out/default/host_x64/zxdb_google_analytics_client_manualtest <tracking-id> <client-id> && echo returned 0
```

Expected result:
- A new event is added to `<tracking-id>`, with event category `test event`,
  event action `test`, event label `test label` and value `12345`.
- The terminal should output:
  ```
  AddEvent success!
  returned 0
  ```

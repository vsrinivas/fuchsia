# Testing the lite Google Analytics client (manually)
Compile the test
```
fx set core.x64  --with-host '//src/lib/analytics/cpp/core_dev_tools:tests'
fx build
```

Run the test
```
out/default/host_x64/analytics_cpp_core_dev_tools_google_analytics_client_manualtest <tracking-id> <client-id>
```

Expected result:
- A new event is added to `<tracking-id>`, with event category `test event`,
  event action `test`, event label `test label` and value `12345`.

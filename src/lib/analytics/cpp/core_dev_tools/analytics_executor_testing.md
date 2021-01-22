# Testing the analytics executor (manually)

The test is an integrated test to make sure libcurl works well with the threading and timeout logic
implemented in AnalyticsExecutor.

Compile the test
```
fx set core.x64  --with-host '//src/lib/analytics/cpp/core_dev_tools:tests'
fx build
```

Run the test
```
out/default/host_x64/analytics_cpp_core_dev_tools_analytics_executor_manualtest <timeout-ms> <url> <post-data>
```


## Things to try (under normal network conditions)

- Set a very short timeout, so that the HTTP request cannot be finished. The program should
  print nothing and quit after the timeout without errors.
- Set a long enough timeout, so that the HTTP request can be finished. The program should print out
  the HTTP response and quit.
- Set a very long timeout. The program should print out the HTTP response and quit, and should
  not wait for the timeout.
- Set timeout to -1 (meaning no timeout). The program should print out the HTTP response and quit.

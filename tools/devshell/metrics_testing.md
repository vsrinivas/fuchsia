# Testing of metrics fx subcommand

## Automated testing

Run `scripts/tests/metrics-tests` and verify the output:
```
$ scripts/tests/metrics-tests
RUNNING: test::enable_metrics
PASSED: test::enable_metrics
RUNNING: test::run_command_with_metrics_disabled
PASSED: test::run_command_with_metrics_disabled
RUNNING: test::run_command_with_metrics_enabled
PASSED: test::run_command_with_metrics_enabled
All 3 tests passed!
PASS
```

## Manual End-to-end testing

* Enable metrics collection with logging

  ```
  fx metrics --log=/tmp/log_metrics.txt enable
  ```

* Execute any fx sub-command, for example:

  ```
  fx set core.x64
  ```

* Verify if a new line was added to the log file, and check the "ea", "el"
  and RESULT arguments. RESULT is the HTTP status code from the server. If it is
  200, it means that the Analytics server accepted the event.

  ```
  cat /tmp/log_metrics.txt
  20190509_132629: v=1 tid=UA-127897021-6 an=fx cid=fe7b7003-b2d6-432b-bae8-ad4791a77667 t=event ec=fx ea=set el=core.x64 RESULT=200

  ```

* If you have access to the Google Analytics account, you will be able to see
  the event happening in realtime, in the GA Realtime tab.

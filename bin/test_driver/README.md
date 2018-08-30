# test driver integration test

This test executes a submodule as passed through a link, along with a test
driver component used to run end-to-end tests on said submodule

The submodule can be any arbitrary mod to test, so long as it is packaged on the
system

Here is an example test file that runs the `driver_example_mod`:

```json
{
  "tests": [
    {
      "name": "driver_example_mod_tap_tests",
      "exec": "device_runner --test --enable_presenter --account_provider=dev_token_manager --device_shell=dev_device_shell --device_shell_args=--test_timeout_ms=60000 --user_shell=dev_user_shell --user_shell_args=--root_module=test_driver_module,--module_under_test_url=driver_example_mod_wrapper,--test_driver_url=driver_example_mod_target_tests --story_shell=dev_story_shell"
    }
  ]
}
```

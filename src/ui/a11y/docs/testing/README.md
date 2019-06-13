# A11y Tests

## Settings Manager Test
Settings Manager Test is an End To End which uses MockSettingsService as
Settings Provider and MockSettingsWatcher to represent clients who wants
to get notified whenever there is a change in the settings. All the test
cases are in settings_manager_test.cc. This test covers the code written
in a11y/a11y_manager/settings.
To run test:
```sh
fx run-test a11y_tests 
```

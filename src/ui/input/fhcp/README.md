Fuchsia Hardware Compatibility Program (FHCP) touchpad tests.

To build and run the tests:

```
fx set core.chromebook-x64 --with //src/ui/input/fhcp:tests
fx build
fx ota
ffx test run fuchsia-pkg://fuchsia.com/touchpad-test#meta/touchpad-test.cm
```

Some tests require manual interaction with the device's touchpad; follow the prompts in the test console output to conduct the test.

# Overview

This directory contains a helper component to allow a test author to use
DriverTestRealm in a simpler way. The SimpleDriverTestRealm will automatically
start and call `fuchsia.driver.test.Realm:Start` with the default arguments.
If a test author is using SimpleDriverTestRealm, then they will not need to
call Start themselves, and they can immediately begin using their DriverTestRealm.

## Usage

Make sure that your executable depends on SimpleDriverTestRealm and the drivers
you want to be included in your test package:

```
test("test") {
   ...
  deps = [
    ...
    "//sdk/lib/driver_test_realm/simple",
    "//path/to/my/awesome-driver",
  ]
}
```

The simplest way to use this is to use the `fuchsia_unittest_package` will
will automatically generate the test CML for you:

```
fuchsia_unittest_package("my_test") {
  deps = [ ":test" ]
}
```

Your test will automatically have `/dev` and DriverManager's other protocols
routed to it, so use them normally. See `test.cc` in this directory for an
example.
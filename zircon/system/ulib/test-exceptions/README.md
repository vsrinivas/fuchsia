# ulib/test-exceptions

This library provides utilities to handle exceptions in unittests. Expected
exceptions can be registered which will catch and handle them appropriately,
whereas unexpected exceptions or failing to catch a registered exception
will trigger a test failure.

This replaced the `RUN_TEST_ENABLE_CRASH_HANDLER` mechanism that used
to be provided by the `unittest` library, both of which have since
been removed. The differences from that mechanism were:

* C++ object instead of macros
* synchronous behavior for simplicity and predictability
* not dependent on any particular test library so can be used equally well with
  the `zxtest` library or the now-removed `unittest` library
* uses new channel-based exception APIs

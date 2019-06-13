# ulib/test-exceptions

This library provides utilities to handle exceptions in unittests. Expected
exceptions can be registered which will catch and handle them appropriately,
whereas unexpected exceptions or failing to catch a registered exception
will trigger a test failure.

This is a replacement for `ulib/unittest`'s `RUN_TEST_ENABLE_CRASH_HANDLER`
mechanism, the differences being:

* C++ object instead of macros
* synchronous behavior for simplicity and predictability
* not dependent on any particular test library so can be used equally well with
 `unittest` or `zxtest`
* uses new channel-based exception APIs

# utest/utc-procargs

Tests used for testing the rules of UTC clock handle distribution in a C runtime
environment.

## Notes

These tests cannot easily be made part of utest/core because they need to be
able to launch another process, something that core tests are not permitted to
do.  The primary behavior under test here is checking to make certain that a C
process which receives a clock handle responsible for distributing system-wide
UTC properly installs the clock in the runtime state during startup, before main
is executed.  The clock should be accessible using the |zx_utc_clock_get|
function exposed in |zircon/utc.h|

These tests specifically do not check that the clock object installed by the
runtime at startup is the clock which is used to satisfy queries such as
clock_gettime.  There are separate tests for checking that behavior.


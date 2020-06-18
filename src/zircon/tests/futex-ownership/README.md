# utest/futex-ownership

Tests used for testing the rules of futex ownership and associated priority
inheritance behavior.

## Notes

These tests are not part of utest/core/futex because they need to be able to
launch another process, something that core tests are not permitted to do.
Specifically, the futex-ownership tests need to make sure that it is not
possible to assign ownership of a futex in process A to a thread from process B.

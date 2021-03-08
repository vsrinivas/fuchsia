# Integration tests

These tests verify that the various components of LLCPP work together to deliver
a particular feature. For example, `handle_rights_test.cc` tests that the client
and server dispatchers verify handle type and down-scope handle rights.
`flexible_test.cc` tests that LLCPP over-allocates memory for flexible types,
in anticipation of receiving unknown fields pushing the message over its
statically computed size limit.

It's good to have a few end-to-end tests verifying the behavior at a high level,
but we should prefer writing tests with a more granular coverage, and over a
smaller interface, such that the resulting tests are easier to setup and
interpret, acknowledging the gap in the status quo.

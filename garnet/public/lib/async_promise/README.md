# libasync_promise

This library provides an implementation of fit::executor that integrates
with async_dispatcher_t.

This code really belongs in Zircon but we are currently unable to compile it
there due to its use of the C++ standard library.  So for now it lives in Garnet
although it largely adheres to Zircon styles, etc.  Once the C++ standard
library is supported in Zircon we will move the code into libasync-cpp where it
really belongs.

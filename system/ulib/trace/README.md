# Trace Library

A static library for instrumenting C and C++ programs to capture trace data.

This library is intended to handle the common case, and is not intended
to be extended beyond that. It is easy enough to write one's own macros
if one wants to, and this library provides lots of boilerplate to begin
from. This library also provides the event_args.h header to assist with this.

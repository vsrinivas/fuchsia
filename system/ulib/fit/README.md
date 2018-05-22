libfit
======

This library contains portable C++ abstractions for control flow and memory
management which meet needs which are not addressed by the C++ 14 standard
library but which are required by essential Fuchsia client libraries,
particularly **libfidl**.

This library only depends on the C++ 14 language and standard library.
The intention is to keep this library extremely small and narrowly focused to
avoid burdening clients with irrelevant dependencies.  It is intended to
be something of an "annex" to the standard library rather than a replacement.

Accordingly, the API style follows C++ standard library conventions.

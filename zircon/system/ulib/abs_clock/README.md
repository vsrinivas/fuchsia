# Abstract clock interface

This library provides a basic abstract clock interface. This simplifies testing
code that needs to read time or sleep.

Instead of reading the time directly, such code should instead accept an
abstract `Clock *` object. In production code, this should be instantiated to a
`RealClock` object, while test code can use a `FakeClock` object, and control
how time advances.

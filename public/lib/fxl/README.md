# Fuchsia Template Library

A library containing basic C++ building blocks, such as logging and
reference counting.

This library builds for both host (this includes Linux, Windows, and
macOS) as well as target (i.e., Fuchsia) platforms and is not source
or binary stable. On both host and target, we build with C++14.

## Should I put my thing in FXL?

In an ideal world, FXL wouldn't exist and we could use the C++ standard
library's building blocks. Unfortunately, the C++ standard library is missing
some important functionality (e.g., logging) and makes some design choices we
disagree with (e.g., `sizeof(std::shared_ptr) == 2*sizeof(void*)`,
`std::chrono` lacking a defined scalar representation). FXL exists to fill in
those gaps in the standard library.

It's easy for these types of libraries to accrete a large amount of code because
they're convenient places to share code. We'd like to keep FXL small and focused
on the problem of "fixing" the C++ standard library, which means you probably
shouldn't put your thing in FXL unless it is related to a particular deficiency
of the C++ standard library.

If you look at the library, you'll see we haven't quite lived up to this ideal,
but hope springs eternal.

## What currently exists in FXL that should be moved out?

Most, if not all, of the code in `files` should be split out into its own
library. That code is already in a separate namespace to make that move easier
in the future.

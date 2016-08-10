# system/ulib/test-utils

This library contains wrappers and utilities to simplify writing tests.
As a general rule one needs to check the result of every system call
or library call. It's important, but it's also a pain.
The wrappers here check the result and only return upon success.
E.g., tu_malloc() only returns if malloc succeeded.
If the call fails the process is terminated.
It's possible to be a bit more clever but for the particular
things that are wrapped, if the call fails there's not much point
in continuing the test. And if there is a point to continuing
the test then don't use these wrappers.
Note that that means that these calls aren't to be
used willy-nilly. If you're testing, say, memory exhaustion with malloc
then you do want to verify that malloc returns NULL and thus you do not
want to use the tu_malloc wrapper for this.

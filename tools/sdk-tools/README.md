# sdk-tools - Temporary switchback for SDK based package management.

These tools are used in the Fuchsia SDK for publishing and serving packages.
They are wrappers around `pm` and `ssh` that make sure the paths and options
used by each of these tools are consistent to reduce the typing needed by developers
and reduce developer friction.

These tools should be seen as a short term solution between now and when
`ffx` package and target management functions are complete. 

Developer workflows or processes should not depend on these tools without first
coordinating with the OWNERS. 


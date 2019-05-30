# Identity Common

## Overview

Identity Common defines a set of utilities that are useful across `src/identity`.


## Key Dependencies

None


## TaskGroup

`TaskGroup` is a type used for spawning and gracefully cancel a group of a dynamic number of
asynchronous tasks, allowing tasks to respond to a cancellation signal and
terminate at their own pace as appropriate. It is intended for longrunning tasks with moderately
complex teardown logic. It was designed for handling teardown of FIDL servers within a component,
but is independent of FIDL.


## Future Work

No future work is currently planned in this crate.


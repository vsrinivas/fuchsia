# Text Field Test Suite

This package can be used to test an implementation of TextField. If you implement a TextField on Fuchsia, during your automated integration tests, you should spin up this package, connect to its TextFieldTestSuite interface, and use it to run the tests.

We'd like all TextFields on the operating system to conform to an identical set of behaviors, so that keyboards don't need to worry about varying implementations. This package (in the future) will test this set of behaviors, including:

- Correctly mutating state in response to edit commands
    - Especially the way that selections and ranges change in response to edits
- Correctly sending OnUpdate events
- Correctly implementing transactional editing
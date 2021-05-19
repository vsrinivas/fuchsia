# //src/sys/pkg/testing

Helper libraries for testing packaging components. Mostly useful for writing
integration tests which test fidl protocols.

For the integration tests themselves, see `//src/sys/pkg/tests`.

## Tips for integration testing

*   When writing mocks for fidl protocols, avoid asserting that responders can
    send the response without error. This is important to prevent integration
    tests from flaking. Otherwise, if the integration test env is torn down
    before a request completes, the mock can panic when trying to respond
    through a closed channel, which would produce a flake.

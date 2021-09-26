# Stress Test Runner

Stress test runner is a test runner that launches a stress test realm and translates
it to the `fuchsia.test.Suite` protocol on behalf of the test.

A stress test realm has the following topology:

                 Test Root
                /         \
       Test Subject        Actors

Actors are components that are created dynamically by the runner in a collection. A test writer
will route capabilities from the test subject to the actor collection. The actors will use these
capabilities to stress the test subject.

The actors are controlled by the runner using a FIDL protocol: `fuchsia.stresstest.Actor`.

Actors offer a set of actions to the runner. The runner coordinates between the different actors,
making each one run different actions in parallel.

The actor library expects exactly one connection to the Actor protocol from the test runner during
the actor's lifetime.

The Test Runner Framework does not see this internal implementation. Stress tests have a single
test case exposed to the test runner framework that will error out when any of the actors fail.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/stress_test
fx build
```


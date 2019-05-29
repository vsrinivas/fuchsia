# Test Fixture Library

This library can be used to eliminate boilerplate code, and in so doing
hopefully avoid the "wait for nothing bad to happen" anti-pattern that can cause
indeterminate ("flaky") results in test code.

## TestFixture
This class encapsulates the gtest methods that wait for async operations, using
four essential pieces: `CompletionCallback()`, `ErrorHandler()`,
`ExpectCallback()` and `ExpectError()`.

By default, `ErrorHandler()` and `CompletionCallback()` each return a basic
function that can be used within the audio test collection as event or error
handlers, or as completion functions. The default error handler sets a bool
`error_occured_` indicating that an error occurred, and stores the error code as
`error_code_`; the default completion handler simply sets a bool
`callback_received_` indicating that a callback was received. One can augment
these behaviors by passing an explicit closure as an argument to ErrorHandler or
CompletionCallback; note that the built-in handler will be called before the
passed-in custom handler.

The `ExpectCallback()` and `ExpectError()` methods run the test's asynchronous
loop while checking for the expected outcome.  Once the expected outcome is
observed, the method runs a few EXPECT checks and returns.  If the expected
outcome is not observed within a timeout period, it triggers a failure before
returning.  By default, ExpectCallback expects only that the member
`callback_received_` has been set, and by default the expected outcome for
ExpectDisconnect is only that `error_occurred_` has been set and `error_code_`
has been set to `ZX_ERR_PEER_CLOSED`.  As with `CompletionCallback()`, if a more
intricate conditional should be satisfied before the Expect call returns, this
can be sent to `ExpectCondition()` as an argument.  Similarly,
`ExpectDisconnect()` is only a special-case of the more general `ExpectError()`
that accepts the error code that should be received.

The Expect timeout interval and polling period are set to 60 seconds and 1
millisecond respectively.  A tight polling interval is not excessively impactful
to system performance because the involved callbacks/disconnects generally
return within 1-3 milliseconds (Observed locally, changing the polling inverval
from 1 ms to 2 ms multiplied the running-time of audio_fidl_tests by 1.86).

The absence of an `ExpectTimeout()` method is intentional.  In cases when the
failure mode presents itself with an asynchronous error handler or exception,
the best practice is to avoid "waiting for nothing bad to happen", and instead
to use an explicit completion signal or some other positive indicator to confirm
that the test case's subject has not crashed or otherwise failed.

To summarize (and to address a common misunderstanding): `CompletionCallback()`
and `ErrorHandler()` return functions that will be triggered by external
asynchronous events; whereas the `ExpectCondition()` function accepts a
__conditional__ that is evaluated to determine whether the synchronously-called
loop should return instead of waiting.

## AudioTestBase
This subclass builds on TestFixture and stores the test binary's component
context, to be used when tests need it in order to connect to another service.
Commonly, a test binary's testing::Environment will set this after retrieving it
at app startup time; otherwise, the class will retrieve and set this before the
first instance of that test is run.

## AudioCoreTestBase
This subclass builds on AudioTestBase and contains an instance of
fuchsia::media::AudioCore that is created anew upon each test case SetUp, and
checked upon each Expect and at test TearDown.

Storing an instance of AudioCore (rather than Audio) is appropriate in this
case, because (so far) the clients of this test library are test binaries that
exist to validate the AudioCore service in targeted ways.

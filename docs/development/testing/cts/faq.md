# Frequently Asked Questions

Are there any examples of CTS tests?

  * Yes!  See [//sdk/cts/examples] for some examples, or peruse the complete set
    of tests under [//sdk/cts/tests].

Is CTS appropriate for unit tests of my service?

  * CTS is used to test ABI and API exposed through Fuchsia SDKs, including (but
    not limited to) FIDL and C/C++ APIs.  Unit tests that exercise FIDL API
    exposed from your service are exactly what we want.  Unit tests that
    exercise functionality that isn't directly exposed to SDK users are out of
    scope.

What is meant by compatibility in the C of CTS?

  * CTS tests are intended to exercise all documented features of API and ABI
    exposed by Fuchsia SDKs.  If we take an older set of tests, and run it
    against a newer platform release, we say that the newer platform release is
    compatible with the older one if it exhibits the same behavior.

What is the compatibility window that CTS attempts to uphold?

  * The goal is to uphold the compatibility window required by the platform,
    which is currently 6 weeks.  We also intend to enforce the requirement for
    soft transitions for API - we will not accept hard breaking changes.

Are CTS tests those that reach into the service or do they access the FIDL or API?

  * CTS tests only target API/ABI exposed through Fuchsia SDKs.

One responsibility Fuchsia.settings.display has is to change the brightness of
the screen. Should the test change the brightness and then assert a response
given that the brightness changed? Or should the test use an emulator to
identify if the brightness actually changed?

  * Tests should generally not assume that they are run on any particular
    device.  However, CTS tests will be run on emulators, and if you would like
    to provide additional useful compatibility testing by leveraging that fact
    (e.g., by instrumenting device drivers), we encourage you to reach out to us
    to discuss further.

How will we know if a test fails?

  * These tests will be run in CQ and will indicate status as part of their
    builder pipeline.

Where will CTS tests run? Emulator? Devices?

  * Initially, CTS tests will be automatically run on Emulators in CQ.
    Developers will also be able to run the tests locally as part of their
    traditional workflows.  Over time, tests are likely to be run on all devices
    as part of qualification.

I added a CTS test. How long until it is running in CQ?

  * CTS tests will begin running in CQ the day after they are rolled into GI.

## Additional questions

For questions and clarification on this document, please reach out to this
directory's owners or file a bug in the [CTS bug component].


[CTS bug component]: https://bugs.fuchsia.dev/p/fuchsia/templates/detail?saved=1&template=Fuchsia%20Compatibility%20Test%20Suite%20%28CTS%29&ts=1627669234
[//sdk/cts/examples]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/cts/examples/
[//sdk/cts/tests]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/cts/tests/

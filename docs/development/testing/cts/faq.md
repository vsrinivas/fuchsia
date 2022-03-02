# Frequently Asked Questions

## What is Fuchsia CTS? {#what-is-cts}

Please see the [CTS overview] for an explanation of what CTS is.

## What is the CTS release schedule? {#schedule}

CTS has multiple releases with separate cadences:

| Release  | Schedule |
|----------|----------|
| Canary   | ~4 hrs   |
| Milestone| ~6 weeks |

The canary release is created when new canary releases of the Fuchsia platform
are created. Likewise, milestone releases are created when new milestone releases
of the Fuchsia platform are created.

Milestone branches (e.g. releases/f7) often receive cherry picks. When this
happens, a new CTS for that milestone is generated and automatically rolled
into CI/CQ.

{% dynamic if user.is_googler %}

Internal contributors: Look for builders named cts*prebuilt-roller in Milo
to monitor new releases.

{% dynamic endif %}

## When will my CTS test start running on CQ? {#wait-time}

The tip-of-tree version of your test will immediately begin running on CI/CQ.
This version of the test does not guarantee backward compatibility.

When the next CTS release is cut, it will contain a snapshot of your test from
tip-of-tree which will begin running as soon as that CTS release rolls into
CI/CQ.  This version of your test guarantees backward compatibility.

See [this section](#schedule) above for the release schedule.

## What test environment does CTS use in CQ? {#environments}

See go/fuchsia-builder-viz. Look for builders whose names end in "-cts".

At minimum, all CTS tests run on the core.x64 image in the Fuchsia emulator.

## What do I do if a CTS test is blocking my CL? {#broken-test}

This is a sign that your CL is breaking a part of the platform surface area.
Please verify that there are no projects in downstream repositories that rely
on the APIs and ABIs modified by your CL. If so, you will need to make a
soft transition. The general worklow is as follows:

1. Submit a CL that introduces the new behavior in your change and verify that
   the tip-of-tree version of the CTS test passes.
1. Notify any downstream SDK Users of the upcoming breaking change, ask them to
   migrate and depend on the new behavior.
1. Wait for the next CTS release to roll into CI/CQ.
1. Submit a CL to remove the old behavior.

## Are there any examples of CTS tests? {#examples}

Yes!  See [//sdk/cts/examples] for some examples, or peruse the complete set
of tests under [//sdk/cts/tests].

## When and why should I write a CTS test? {#why-cts}

You should write a CTS test if:

1. Your software is part of the public or parnter SDKs.
2. You want CQ to prevent backward-incompatible changes to your software
   across multiple releases of the Fuchsia platform.

## Additional questions

For questions and clarification on this document, please reach out to this
directory's owners or file a bug in the [CTS bug component].


[CTS bug component]: https://bugs.fuchsia.dev/p/fuchsia/templates/detail?saved=1&template=Fuchsia%20Compatibility%20Test%20Suite%20%28CTS%29&ts=1627669234
[CTS overview]: /docs/development/testing/cts/overview.md
[//sdk/cts/examples]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/cts/examples/
[//sdk/cts/tests]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/cts/tests/

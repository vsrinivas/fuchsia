# Contributing Patches to Zircon

At this point in time, Zircon is under heavy, active development, and we're
not seeking major changes or new features from new contributors, however, if
you desire to [contribute](/CONTRIBUTING.md), small bugfixes are welcome.

Here are some general guidelines for patches to Zircon.  This list is
incomplete and will be expanded over time.

[TOC]

## Process


* Follow the process for Fuchsia patches outlined in [CONTRIBUTING.md](/CONTRIBUTING.md).

*   Patches are handled via Gerrit Code Review at: https://fuchsia-review.googlesource.com/#/q/project:zircon

*   Additionally, make sure Zircon is buildable for all major targets (x86-64, arm64) at every
    change. Using `fx multi bringup-cq` can help with this.
    See [Building Zircon for all targets](/docs/zircon/getting_started.md#building_zircon_for_all_targets)
    for more information.

*   Avoid breaking the unit tests. Boot Zircon and [run the tests](/docs/development/testing/testing.md) to verify that
    they're all passing.

*   Avoid whitespace or style changes. Especially do not mix style changes with
    patches that do other things as the style changes are a distraction. Use `fx format-code`
    to format the code with the consistent style.

*   Avoid changes that touch multiple modules at once if possible. Most changes
    should be to a single library, driver, app, etc.

## Documentation

* Documentation is one honking great idea &mdash; let's do more of that!

    - Documentation should be in Markdown files. Zircon documentation is located in [/docs/zircon][googlesource-docs].
      Please check how your docs are rendered.

    - Some notable docs: there's a list of syscalls in [/docs/reference/syscalls/README.md][syscall-doc] and a list of
      kernel cmdline options in [/docs/reference/kernel/kernel_cmdline.md][cmdline-doc].  When editing or adding
      syscalls or cmdlines, update the docs!

## Notes

### How to deprecate #define constants

One can create a deprecated typedef and have the constant definition
cast to that type.  The ensuing warning/error will include the name
of the deprecated typedef.

```
typedef int ZX_RESUME_NOT_HANDLED_DEPRECATION __attribute__((deprecated));
#define ZX_RESUME_NOT_HANDLED ((ZX_RESUME_NOT_HANDLED_DEPRECATION)(2))
```

[googlesource-docs]: /docs/zircon/
[syscall-doc]: /docs/reference/syscalls/README.md
[cmdline-doc]: /docs/reference/kernel/kernel_cmdline.md


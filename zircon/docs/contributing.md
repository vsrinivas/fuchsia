# Contributing Patches to Zircon

At this point in time, Zircon is under heavy, active development, and we're
not seeking major changes or new features from new contributors, however, if
you desire to [contribute](/docs/CONTRIBUTING.md), small bugfixes are welcome.

Here are some general guidelines for patches to Zircon.  This list is
incomplete and will be expanded over time.

[TOC]

## Process

*   GitHub pull requests are not accepted. Patches are handled via Gerrit Code
    Review at: https://fuchsia-review.googlesource.com/#/q/project:zircon

*   The #fuchsia channel on the freenode irc network is a good place to ask
    questions.

*   Include [tags] in the commit subject flagging which module, library, app,
    etc, is affected by the change. The style here is somewhat informal. Look at
    past changes to get a feel for how these are used. Gerrit will flag your
    change with `Needs Label: Commit-Message-has-tags` if these are missing.

*   Include a line like `Test: <how this was tested>` in the commit message, or
    else Gerrit will flag your change with `Needs Label:
    Commit-Message-has-TEST-line`.

    *   The full (Java-style) regex is `(?i:test|tests|tested|testing)[
        \t]*[=:]`, so lines like `TESTED=asdf` or `Testing : 1234` are also
        valid, along with lines that only contain `Test:` with the details on
        the following lines.

*   [Googlers only] Commit messages may reference issue IDs, which will be
    turned into links in the Gerrit UI. Issues may also be automatically closed
    using the syntax `BUG-123 #done`. *Note*: Zircon's issue tracker is not open
    to external contributors at this time.

*   Zircon should be buildable for all major targets (x86-64, arm64) at every
    change. ./scripts/build-all-zircon can help with this.

*   Avoid breaking the unit tests. Boot Zircon and run "runtests" to verify that
    they're all passing.

*   Avoid whitespace or style changes. Especially do not mix style changes with
    patches that do other things as the style changes are a distraction.

*   Avoid changes that touch multiple modules at once if possible. Most changes
    should be to a single library, driver, app, etc.

## Documentation

* Documentation is one honking great idea &mdash; let's do more of that!

    - Documentation should be in Markdown files.  Our Markdown is rendered in Gitiles in
      [the main repo browser][googlesource-docs]. Please check how your docs are rendered.

    - Some notable docs: there's a list of syscalls in [docs/syscalls.md][syscall-doc] and a list of
      kernel cmdline options in [docs/kernel_cmdline.md][cmdline-doc].  When editing or adding
      syscalls or cmdlines, update the docs!

    - [The `h2md` tool][h2md-doc] can scrape source files and extract embedded Markdown.  It's
      currently used to generate API docs for DDK.  `./scripts/make-markdown` runs `h2md` against
      all source files in the `system/` tree.

[googlesource-docs]: /zircon/docs/
[syscall-doc]: /zircon/docs/syscalls.md
[cmdline-doc]: /zircon/docs/kernel_cmdline.md
[h2md-doc]: /zircon/docs/h2md.md

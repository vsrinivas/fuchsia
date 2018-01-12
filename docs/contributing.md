# Contributing Patches to Zircon

At this point in time, Zircon is under heavy, active development, and we're
not seeking major changes or new features from new contributors, however, if
you desire to [contribute](https://fuchsia.googlesource.com/docs/+/master/CONTRIBUTING.md), small bugfixes are welcome.

Here are some general guidelines for patches to Zircon.  This list is
incomplete and will be expanded over time.

[TOC]

## Process

* GitHub pull requests are not accepted.  Patches are handled via
  Gerrit Code Review at: https://fuchsia-review.googlesource.com/#/q/project:zircon

* The #fuchsia channel on the freenode irc network is a good place to ask
questions.

* Include [tags] in the commit subject flagging which module, library,
app, etc, is affected by the change.  The style here is somewhat informal.
Look at past changes to get a feel for how these are used.  Gerrit will flag
your change with `Needs Label: Commit-Message-has-tags` if these are missing.

* [Googlers only] Commit messages may reference issue IDs, which will be
turned into links in the Gerrit UI. Issues may also be automatically closed
using the syntax `BUG-123 #done`.  *Note*: Zircon's issue tracker is not open
to external contributors at this time.

* Zircon should be buildable for all major targets (x86-64, arm64)
at every change.  ./scripts/build-all-zircon can help with this.

* Avoid breaking the unit tests.  Boot Zircon and run "runtests" to
verify that they're all passing.

* Avoid whitespace or style changes.  Especially do not mix style changes
with patches that do other things as the style changes are a distraction.

* Avoid changes that touch multiple modules at once if possible.  Most
changes should be to a single library, driver, app, etc.

## Style

* Code style mostly follows the [Google C++ Style Guide][google-style-guide], except:

    - When editing existing code, match local style.  This supersedes the other style rules.

    - Maximum line width is 100 characters rather than 80.

    - Indentation is four spaces, not two.  Still never use tabs.  Do not leave trailing whitespace
      on lines.  Gerrit will flag bad whitespace usage with a red background in diffs.

    - Zircon declares pointers as `char* p` and **not** as `char *p;`.  The Google style guide is
      ambivalent about this; Zircon standardized on asterisk-with-type instead of
      asterisk-with-name.

    - Inside a `switch` statement, the `case` labels are not indented.  Example:

          void foo(int which) {
              switch (which) {
              case 0:
                  foo_zero();
                  break;
              case 17:
                  foo_seventeen();
                  break;
              default:
                  panic("I don't know how to foo here (which = %d)\n", which);
              }
          }

    - Put curly braces around the body of any loop and in any `if` statement where the body is on
      another line (including any `if` statement that has an `else if` or `else` part). Trivial `if`
      statements can be written on one line, like this:

          if (result != ZX_OK) return result;

       However, do **not** write:

          if (result == ZX_OK)
              return do_more_stuff(data);
          else
              return result;

       and do **not** write:

          while (result == ZX_OK)
              result = do_another_step(data);

      Note that this rule isn't enforced by `clang-fmt`, so please pay attention to it when writing
      code and reviewing commits.

* You can run `./scripts/clang-fmt` to reformat files.  Pass in the filenames as arguments, e.g.

      scripts/clang-fmt kernel/top/main.c kernel/top/init.c

  The `clang-fmt` script will automatically download and run the `clang-format` binary.  This fixes
  most style problems, except for line length (since `clang-format` is too aggressive about
  re-wrapping modified lines).

## Documentation

* Documentation is one honking great idea &mdash; let's do more of that!

    - Documentation should be in Markdown files.  Our Markdown is rendered both in Gitiles in
      [the main repo browser][googlesource-docs] and in Github in [the mirrored repo][github-docs].
      Please check how your docs are rendered on both websites.

    - Some notable docs: there's a list of syscalls in [docs/syscalls.md][syscall-doc] and a list of
      kernel cmdline options in [docs/kernel_cmdline.md][cmdline-doc].  When editing or adding
      syscalls or cmdlines, update the docs!

    - [The `h2md` tool][h2md-doc] can scrape source files and extract embedded Markdown.  It's
      currently used to generate API docs for DDK.  `./scripts/make-markdown` runs `h2md` against
      all source files in the `system/` tree.

[google-style-guide]: https://google.github.io/styleguide/cppguide.html
[googlesource-docs]: https://fuchsia.googlesource.com/zircon/+/master/docs/
[github-docs]: https://github.com/fuchsia-mirror/zircon/tree/master/docs
[syscall-doc]: https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls.md
[cmdline-doc]: https://fuchsia.googlesource.com/zircon/+/master/docs/kernel_cmdline.md
[h2md-doc]: https://fuchsia.googlesource.com/zircon/+/master/docs/h2md.md

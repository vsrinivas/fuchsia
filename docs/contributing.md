# Contributing Patches to Magenta

At this point in time, Magenta is under heavy, active development, and we're
not seeking major changes or new features from new contributors, but small
bugfixes are welcome.

Here are some general guidelines for patches to Magenta.  This list is
incomplete and will be expanded over time:

* GitHub pull requests are not accepted.  Patches are handled via
  Gerrit Code Review at: https://fuchsia-review.googlesource.com/#/q/project:magenta

* Indentation is with spaces, four spaces per indent.  Never tabs.
Do not leave trailing whitespace on lines.  Gerrit will flag bad
whitespace usage with a red background in diffs.

* Match the style of surrounding code.

* Avoid whitespace or style changes.  Especially do not mix style changes
with patches that do other things as the style changes are a distraction.

* Avoid changes that touch multiple modules at once if possible.  Most
changes should be to a single library, driver, app, etc.

* Include [tags] in the commit subject flagging which module, library,
app, etc, is affected by the change.  The style here is somewhat informal.
Look at past changes to get a feel for how these are used.

* Magenta should be buildable for all major targets (x86-64, arm64, arm32)
at every change.  ./scripts/build-all-magenta can help with this.

* Avoid breaking the unit tests.  Boot Magenta and run "runtests" to
verify that they're all passing.

* The #fuchsia channel on the freenode irc network is a good place to ask
questions.

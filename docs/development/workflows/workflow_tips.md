# Workflow tips

This is a list of tips that should help you be more productive when working on fuchsia.

## Gerrit Monitor

Install the [Gerrit Monitor](https://chrome.google.com/webstore/detail/gerrit-monitor/leakcdjcdifiihdgalplgkghidmfafoh)
Chrome extension to have in the Chrome toolbar the list of CLs requiring your
attention.

## Enabling three-way diffs in Git

By default Git uses two-way diffs when presenting conflicts. It does not
display what the original text was before the conflict, which makes it [hard to
solves some conflicts](https://stackoverflow.com/questions/4129049/why-is-a-3-way-merge-advantageous-over-a-2-way-merge).

You can configure Git to show the original text by enabling three-way diffs:

```git config --global merge.conflictstyle diff3```

## Enabling fuchsia-specific git commands

Add `$FUCHSIA_REPO/scripts` to your PATH to be able to use fuchsia-specific git
commands such as `git fuchsia-review [<commit ref>]`, which opens the current
or given commit in gerrit.


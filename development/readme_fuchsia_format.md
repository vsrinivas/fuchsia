# README.fuchsia File Syntax

*README.fuchsia* files are used to annotate third-party source
libraries with some useful metadata, such as code origin, version
and or license.

The format of these files consists of one or more directive lines,
followed by unstructured description and notes.

Directives consist of a directive keyword at the beginning of the line,
immediately followed by a colon and a value that extends to the end of
the line. The value may have surrounding whitespace, and blank lines may
appear before or between directives.

Several directives are described below, but other directives may
appear in *README.fuchsia* files and software that consumes them should
not treat the appearance of an unknown directive as an error. Similarly,
such software should match directive keywords case-insensitively.

Description lines are optional and and follow a "Description:" directive
that must appear on a line by itself prior to any unstructured description
text.

## Syntax

```
file                  := directive-line* description?
directive-line        := directive | blank-line
directive             := keyword ":" SPACE* value SPACE* EOL
value                 := NONBLANK ANYCHAR*
description           := description-directive description-line*
description-directive := "Description:" SPACE* EOL
description-line      := ANYCHAR* EOL
keyword               := [A-Za-z0-9][A-Za-z0-9 ]*
blank-line            := SPACE* EOL
SPACE                 := any whitespace character
EOL                   := end of line character
NONBLANK              := any non-whitespace, non-EOL character
ANYCHAR               := any character but EOL
```

## Common directives keywords

Common directive keywords include:

* Name

  Indicates the component's name. This should be included if the name
  is not obvious from context. Example:

  `Name: OpenSSH`

* URL

  Provides a URL to the upstream software or vendor.
  If the imported third-party component is based on a specific upstream
  release then list that explicitly. Example:

  `URL: https://ftp.openbsd.org/pub/OpenBSD/OpenSSH/openssh-7.6.tar.gz`

  Otherwise, list the vendor's website. Example:

  `URL: https://www.openssh.com/`

  This directive may be repeated to include multiple website URLs if
  necessary.

* Version

  Lists a version number or commit identifier for the software. If the
  version is apparent from the *URL* or commit history, then this may be
  omitted. Example:

  `Version: 7.6`

* Upstream Git

  Links to the upstream git repository from which this component has
  been branched. This should be included for any software branched from
  an external git repository. Example:

  `Upstream Git: https://github.com/openssh/openssh-portable`

* Description

  Marks the end of directives and the beginning of unstructured description.
  It must appear on a line by itself.

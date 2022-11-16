# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Code submission and review
#### EXECUTABLE=${HOST_TOOLS_DIR}/mdlint
### Markdown linter
## USAGE:
##     fx mdlint [flags]
## Flags:
##  -enable value
##        Enable a rule. Valid rules are 'bad-headers', 'bad-lists', 'casing-of-anchors', 'newline-before-fenced-code-block', 'no-extra-space-at-start-of-doc',
##                    'no-extra-space-on-right', 'respect-col-length', 'respectful-code', 'simple-utf8-chars',
##       'verify-internal-links'. To enable all rules, use the special 'all' name
##  -filter-filenames string
##        Regex to filter warnings by their filenames
##  -json
##        Enable JSON output
##  -root-dir value
##        (required) Path to root directory containing Markdown files

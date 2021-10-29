# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Test
### Finds and runs tests affected by the current change.

## USAGE:
##   fx smoke-test [--verbose] [--dry-run] [fx test args]
##
##   --verbose: print verbose messages
##   --dry-run: don't run affected tests (useful with --verbose)
##   Additional args will be passed to `fx test` if affected tests are found.
##
##   Recommended: fx -i smoke-test
##   Runs smoke testing every time you save changes to your work.

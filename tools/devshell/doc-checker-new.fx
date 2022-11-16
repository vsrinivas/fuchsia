# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Code submission and review
#### EXECUTABLE=${HOST_TOOLS_DIR}/doc-checker-new
### Check the markdown documentation using a variety of checks.
## USAGE:
##     fx doc-checker-new [--root <root>] [--project <project>] [--docs-folder <docs-folder>] [--local-links-only]
## Options:
##  --root            path to the root of the checkout of the project.
##  --project         name of project to check, defaults to fuchsia.
##  --docs-folder     (Experimental) Name of the folder inside the project  which
##                    contains documents to check. Defaults to 'docs'.
##  --local-links-only do not resolve http(s) links
##  --help            display usage information

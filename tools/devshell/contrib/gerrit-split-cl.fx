# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Code submission and review
#### EXECUTABLE=${FUCHSIA_DIR}/tools/devshell/contrib/gerrit-split-cl-lib/split_cl.py
### Submits chains of CLs to Gerrit
## #usage: fx split_cl [-h] [--dry_run] [--repo REPO] --cl CL --patch N
##
## Split a single CL into multiple CLs and upload each of those CLs to Gerrit.
## This command should be run from the directory containing your Git checkout.
##
## For example, if you've created a single large CL with Gerrit CL number 1234
## and the most recent patchset number is 4, you can split that into multiple
## CLs with the following command:
##
##   fx split_cl --cl=12345 --patch=4
##
## This script will:
##
##   1. Download the specified patchset to your local Git checkout.
##
##   2. Determine the set of files changed by that patchset, then open your
##      $EDITOR with a configuration file that allows you to specify how the
##      patchset should be split into multiple CLs. Comments at the top of
##      the configuration file explain how this is done. When you are satisfied,
##      save the file and close your $EDITOR.
##
##   3. Create a local Git branch for each CL specified in the prior step.
##
##   4. Upload each Git branch to gerrit. The uploaded CLs will use the same
##      commit message as the original CL, except the first line will be
##      prefixed by one or more tags (such as "[fidl]") which are automatically
##      determined based on the set of changed files. All CLs will be tagged
##      with the same gerrit topic.
##
##   5. Finally, after all CLs are uploaded, the script will checkout JIRI_HEAD.
##
## optional arguments:
##   -h, --help   show this help message and exit
##   --dry_run    If true, print commands that would run but don't execute any of them.
##   --repo REPO  Gerrit repo to connect to. Defaults to https://fuchsia-review.googlesource.com/fuchsia

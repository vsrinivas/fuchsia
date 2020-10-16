# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Scrutiny Example: Searching Files
#
# This example describes how to search for files using the search.packages
# tool.
#
# 1. Search packages support regex matching so you can specify the exact file
#    you are looking for.
print "List all the files exactly matching zbi"
search.packages --files ^zbi$

# 2. Search for a specific package using regex. Notice how its version is v2?
print "Search for the appmgr component"
search.components --url appmgr.cm$

# 3. Search for a few more packages this time using v1 pathing.
print "Search for network based components"
search.components --url netstack.cmx

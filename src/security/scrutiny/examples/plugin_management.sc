# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Scrutiny Example: Plugin Management
#
# This example describes how to load and unload plugins.
# tool.
#
# 1. List all the plugins and their current state.
print "List the status of all current plugins."
engine.plugin.list

# 2. Unload a plugin. Notice that plugin.unload is a builtin command not part
#    of a plugin.
print "Unload the search plugin."
plugin.unload SearchPlugin

# 3. Reprint the status of the plugins, notice that the search plugin is
#    disabled.
print "List the status of all plugins now notice SearchPlugin is Unloaded."
engine.plugin.list

# 4. Reload the search plugin.
print "Reload the search plugin."
plugin.load SearchPlugin

# 5. List all the data collectors currently loaded.
print "List Data Collectors."
engine.collector.list

# 6. List all the data controllers currently loaded
print "List Data Controllers."
engine.controller.list

# 7. Reschedule the data collectors to run.
print "Reschedule the data collectors to be re-run"
engine.collector.schedule

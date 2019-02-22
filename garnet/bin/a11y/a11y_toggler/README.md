# A11y Toggler

A small program to turn on or off accessibility support by making calls to
fuchsia::accessibility::Toggler. Run this program with:

run a11y_toggler [true/false]

Requires an a11y_manager process that implements fuchsia::accessibility::Toggler service
to be available to connect. The current method for starting a11y_manager should be
dynamically by sysmgr when a client tries to connect to one of a11y_manager's FIDL protocols.
This is done by listing the protocols a11y_manager implement in
garnet/bin/sysmgr/config/services.config.

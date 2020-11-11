# System Health Check

This crate exposes a helper function that determines if a running system is
healthy after an OTA, and if it is, informs the paver service.

Since a system can be configured to either use the system-update-checker or
omaha-client components to discover and initiate base package updates and both
components need the ability to check and update the system health, this
functionality is maintained in this crate shared by both components.
# System Health Check

This crate exposes 2 helper functions, one to determine if a running system is
healthy after an OTA, and another to inform the paver service that the current 
system is healthy.

Since a system can be configured to either use the system-update-checker or
omaha-client components to discover and initiate base package updates and both
components need the ability to check the system health, this functionality is
maintained in this crate shared by both components.


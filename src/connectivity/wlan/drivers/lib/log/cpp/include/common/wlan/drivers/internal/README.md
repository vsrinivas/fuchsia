# For internal use by logging library.

The headers in this directory are not intended to be included by drivers. This is currently only
intended for use by the logging library for throttling logs. If some of these headers are found
to be useful for WLAN drivers in the future, they can be moved out of `internal/` after proper
evaluation.
# Diagnostics tests

## What is this test?

This end to end test ensures that various diagnostics services are
actually running on system builds.

## Fuchsia Components involved

The following components are searched for in `ps` output:

* log-stats.cm
* triage-detect.cm
* sampler.cm

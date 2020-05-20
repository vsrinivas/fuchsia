# Timezone tests

This directory contains the tests that verify that the local date-time reported by
a program running inside the Dart VM matches the system settings.

This therefore verifies that the Dart VM (and by extension also Flutter) see
exactly the same local time.

# Approach

Test does the following in one synchronous sequence:

* Sets the system time zone to "America/Los_Angeles" through the 
  `fuchsia.settings.Intl` service. This time zone's offset to UTC will vary
  depending on the time of year.  The default system time zone is normally UTC,
  so this change should result in the change of the system clock throughout.
* Starts the [dart timestamp server](../timestamp-server-dart/README.md).  This
  server reports what the dart VM sees as the current time zone.  The dart VM
  should be using `fuchsia.intl.PropertyProvider` to get the current time zone.
  At the moment dart VM is unable to listen to time zone changes, so it will 
  keep whatever time zone exists at its startup.
* Asks the dart timestamp server about current day and hour, and compares to
  its own reading of the same information.  This is not the complete time, but
  should be enough to detect typical timezone bugs.
* Restores the original system time zone.

# Flakiness note

## Setting time

Since the test deals with real system time, it may be flaky.  We are comparing
two unsynchronized clocks and checking if they are in sync up to the 1 hour
resolution.  This may lead to flakiness if our tests just happen to run close
to the turn of the hour, since the random drifts around the hour start may
cause the tests to be up to 1 hour apart.

For this reason, the test is actually ran up to 3 times in the case a 
discrepancy exists between the readings, and will only fail if we see wrong time
thrice in a row.

## Timezone upset

This tests modifies the system time zone necessarily.  It makes a best effort
to restore the time zone, but may well not succeed to do so. (It's a rust
`drop`, which apparently is not guaranteed to run.)  So, while in practice 
so far we've seen the timezone be returned to its original value, it is not 
always going to happen.

# Building

```
fx set ... --with=/src/tests/intl:tests
fx build
```

# Running the test

```
fx test //src/tests/intl/timezone
```

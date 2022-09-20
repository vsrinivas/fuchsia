# tzdata & zoneinfo version test

This test verifies that

*   the [drop-in Time Zone .res files][unicode-tz-res] used by `icu::TimeZone`,
    and
*   the zoneinfo tzif2 files used by `absl::TimeZone` and `cctz`

are kept in sync to the same upstream revision. This ensures that components
that rely on the Fuchsia platform to provide time zone data will see a
consistent set of time zone definitions, regardless of which of the two
libraries they use.

## Building and Tesing

```bash
fx set core.x64 --with //src/tests/intl/tzdata
# ... start fx serve and fx emu
fx test //src/tests/intl/tzdata
```

[unicode-tz-res]: https://unicode-org.github.io/icu/userguide/datetime/timezone/#icu4c-tz-update-with-drop-in-res-files-icu-54-and-newer

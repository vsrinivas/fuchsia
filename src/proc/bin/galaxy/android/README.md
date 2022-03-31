# Android Galaxy

This galaxy is configured to use a minimal Android system image.

The galaxy runs `system/bin/init` on startup, and waits for
`/linkerconfig/ld.config.txt` to exist before running any comopnents.

For test components that do not need to run init, see the Android Test Galaxy.
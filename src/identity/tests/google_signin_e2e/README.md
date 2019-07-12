# Google Signin E2E Test

## Overview

This is an host-driven end-to-end test that drives the Google OAuth login on a
remote Fuchsia device to test the authentication flow using the Google identity
system.  This test is intended to run only on the workstation product.  The
test consists of a mod that runs on the remote device and triggers the Google
login flow, and a test suite that runs host side and drives the login flow.

## Runtime Dependencies

  * *sl4f* SL4F is used to setup Chromedriver connections.

  * *Chromedriver* Chromedriver is used to drive the authentication flow
    running on Chromium.

  * *tiles_ctl* tiles_ctl is used to start the test mod.


## Running

All commands should be executed on the host machine.

* Run fx set using the workstation product and include the
//src/identity/tests/google_signin_e2e:test target
* fx build
* Download the latest version of Chromedriver from the
[Chromium website](http://chromedriver.chromium.org/downloads).
* Ensure fx serve is running
* Run the `run-signin-e2e` script, passing in the path to the downloaded
Chromedriver binary and authentication credentials.  __Do not use your Google
account credentials!  Only use Owned Test Account credentials!__
```
./run-signin-e2e --chromedriver <path-to-chromedriver> \
    --test-account-email <test account email> \
    --test-account-password <test account password>
```

## Future work

The test mod and tiles_ctl are currently used to start the login flow, these
will be removed in favor of starting login using a mechanism that exists as
part of a product.

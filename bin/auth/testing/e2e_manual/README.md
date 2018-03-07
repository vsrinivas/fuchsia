Auth Provider Manual Integration tests
========================================

This directory contains an "end-to-end" test suite for the entire OAuth flow and
needs a working refresh token for a test user authorized for Fuchsia OAuth
client-ID to run correctly.

To run this test suite, use the following command:

/system/bin/google_oauth_demo --user-profile-id=<USER-PROFILE-ID>
--refresh-token=<OAUTH-REFRESH-TOKEN>

where:
USER-PROFILE-ID is the user's unque identifier returned by the Identity Provider,
and
OAUTH-REFRESH-TOKEN is user's refresh token minted for Fuchsia OAuth Client.


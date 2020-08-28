httpdate-hyper
============

This library retrieves time by making HTTPS calls to some server and extracting
the time from the response Date header.

## Testing

The library is tested by running a local HTTPS server and pointing the library
to make requests against it. The test server signs responses using the test 
certificates contained in the `certs/` directory. These certificates should not
be used outside of testing.

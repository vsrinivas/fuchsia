# README

The `example-tester` is a testing utility for the FIDL canonical examples,
located at `//examples/fidl/*`.

The purpose of this utility is to enable easy testing of FIDL canonical examples
in such a way that does not compromise their utility or compactness as examples.
To this end, all test assertions done by this framework are done on the log
output produced by the components under test.

For a demonstration of how to use this utility, please see the adjacent
`/example` directory.

The utility accommodates three test scenarios:

- *Client <-> Proxy <-> Server*: There are three components under test. The
  first of these, the "client", sends a request to the proxy, which forwards the
  request to the server, which does some action based on the request, possibly
  (but not necessarily) sending a response which is piped all the way back to
  the client. At a pre-determined end point (usually the client process
  exiting), the test shuts down, and log assertions are performed.
- *Client <-> Server*: There are two components under test. The first of these,
  the "client", sends a request to the server, which does some action based on
  the request, possibly (but not necessarily) sending a response which is sent
  back to the client. At a pre-determined end point (usually the client process
  exiting), the test shuts down, and log assertions are performed. This is the
  most common testing scenario.
- *Standalone*: There is one component under test. It does not perform any IPC
  messaging, and instead does all of its work-under-test locally, perhaps
  writing some persistent FIDL to disk, or doing some other non-IPC action.

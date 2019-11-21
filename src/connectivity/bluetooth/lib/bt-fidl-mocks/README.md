This library provides light weight mocks of Fuchsia SDK Bluetooth FIDL protocols to facilitate
unit testing of clients. The emulation of the interfaces is minimal and requires the test authors to
drive the state and behavior of each protocol by setting up expectations on individual FIDL calls.

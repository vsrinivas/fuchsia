# Synchronization tests

This directory contains a test suite for the Ledger synchronization. This is an "end-to-end" test and needs a working Firebase instance to run correctly.

To run this test suite, use the following command:
```
/system/test/ledger_sync_test --server-id=<FIREBASE_ID>
```
where `FIREBASE_ID` is the instance name of a Firebase instance.

# Synchronization tests

This directory contains an "end-to-end" test suite for synchronization and needs a working Firebase instance to run correctly. It uses the synchronization tests from the integration test suite.

To run this test suite, use the following command:
```
/system/test/ledger_sync_test --server-id=<FIREBASE_ID>
```
where `FIREBASE_ID` is the instance name of a Firebase instance.

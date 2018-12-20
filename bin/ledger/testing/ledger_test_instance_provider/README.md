# Ledger Test Instance Provider

This is a package containing a `ledger_test_instance_provider_bin` binary whose
purpose is to return a single Ledger instance backed by memfs.

It exists to allow tests built outside of peridot to get access to a Ledger
instance, a task otherwise impossible because the required API
(LedgerRepository) is not available outside of peridot.

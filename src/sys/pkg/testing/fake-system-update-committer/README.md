This is a fake system-update-committer component which has no dependencies.

It will always assert that the system is committed, which is useful for testing
as well as bringing up OTAs on a system which does not have ability to check
whether the underlying system is healthy (like a recovery partition).
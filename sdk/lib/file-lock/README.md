# libfile-lock

This library implements an lock mechanism
and handles contention between read and write locks.
Only a single client (as defined by KOID) can
own a write lock, but many clients can own read locks.

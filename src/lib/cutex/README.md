Cutex
-----

A Rust async mutex that:
    - is fair under contention
    - can be conditionally acquired via a 'lock_when(predicate)' function
    
Before locking, a waiters predicate is checked and if the predicate is
not true that waiter stays waiting in the same place in the waiter
queue.

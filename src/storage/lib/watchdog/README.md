# FS watchdog

watchdog provides a way to track progress of an operation. Each operation has a
time scope - a start time and estimated time to complete. When an operation does
not complete in its estimated time, watchdog can take a set of action including
printing messages.

By default, when an operation times out, watchdog prints stack traces of all the
threads in the process. On timeout, watchdog also calls operation specific
timeout handlers. To avoid flooding logs with backtraces, watchdog tries to
print backtraces only once per timed out operation.

Watchdog tracks operations with OperationTracker. OperationTracker describes the
operation properties like

*   Name
*   Timeout value
*   Start time of the operation
*   Timeout handler

OperationTracker is not owned by the Watchdog. This allows Tracker to be created
on stack(without malloc) or allows Tracker to be part of a different class that
implements the base class.

## Usage

There are helper structures for common operation types in filesystems, which can
be used in most cases:

```C++
  // Following lines can go in some header file in Myfs
  static const FsOperationType kMyfsAppendOperation(CommonFsOperation::Append,
                                                      std::chrono::nanoseconds(100));

  // This may happen during mount or once per process lifetime.
  auto watchdog = CreateWatchdog(kDefaultOptions);
  EXPECT_TRUE(watchdog->Start().is_ok());

  // This can be somewhere in operations like read/write/append/...
  // This is the only line that should be added to places you want to track.
  FsOperationTracker tracker(&kMyfsAppendOperation, watchdog.get());
```

For better control or to print detailed operation specific information through
OnTimeout(), one can have the following.

```C++
  // In some header file in myfs
  class MyfsOperationTracker : public FsOperationTracker {
   public:
    explicit MyfsOperationTracker(const OperationBase* operation, WatchdogInterface* watchdog,
                                   bool track = true)
        : FsOperationTracker(operation, watchdog, track) {}
    void OnTimeOut(FILE* out_stream) const final {
      fprintf(out_stream, "%d", my_awesome_data_);
    }
   private:
     int my_awesome_data_ = 42;
  };
```

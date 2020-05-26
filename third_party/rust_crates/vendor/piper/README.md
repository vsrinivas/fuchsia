# Piper

Async pipes, channels, mutexes, and more.

> **NOTE:** This crate is still a work in progress. Coming soon.

- Arc and Mutex - same as std except they implement asyncread/asyncwrite
- Event - for notifying async tasks and threads, advanced AtomicWaker
- Lock - async lock
- chan - Sender and Receiver implement Sink and Stream
- pipe - Reader and Writer implement AsyncRead and AsyncWrite

## TODO's

 - change w.await to listener.await

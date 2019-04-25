# IO Scheduler

IO Scheduler is a library that provides a generic IO scheduling queue and thread pool to service it.

## Overview

The Fuchsia system architecture pushes subsystems that would be inside a traditional monolithic kernel into separate user-space services. In doing so, it decouples the servicing threads from its clients. This decoupling loses important priority information that was implicitly transmitted along with IO requests to this subsystem. This library is part of the solution to maintain priority information along with IO requests that cross process boundaries.

## Concepts

* **Client** - The code using this library. This document defines the API surface used by the client code.

* **Op** - The library schedules operations, or `ops`. These are of type ``class StreamOp``. An IO operation is a discrete unit of IO that is meaningful to that driver stack. Examples are: reading of a sequence of bytes, scatter-gather writing a set of buffers, or a flush of caches. An op may be completed synchronously or asynchronously. An op can be in the following states:
  * **Acquired** - an op arrives into the scheduler from a request source.
  * **Issued** - the op is transmitted to the client-provided execution callback for immediate execution. The execution may be synchronous or asynchronous.
  * **Completed** - The client has reported that the operation completed with either success or error.
  * **Released** - The op is returned to the requestor with status.

* **Stream** - A stream is a logical sequence of operations. The stream is the basic unit of priority scheduling. Streams compete for op issues based on their priorities. Operations within a stream may be reordered with respect to each other, within some constraints. Ordering between streams is unpredictable unless explicit synchronization is used. An op is considered **earlier** if it is closer to being issued than another, and **later** otherwise. In a strict FIFO an earlier op is one that has been pushed into the FIFO before another.

* **Group** - A group is a set of related operations that must all succeed or else all will fail. A group’s members will not be reported as complete until all have succeeded or at least one of them failed. If one member of a group fails, the scheduler may skip issuing all unissued members of the group and mark them as failed. Groups may span across streams.

* **Barrier** - A barrier is an operation that constrains the scheduler’s reordering ability within a stream. When combined with groups, they can synchronize across streams. A barrier describes a condition that blocks some class of ops from being reordered around it. A barrier is cleared once all operations earlier than the barrier that meet the barrier condition have been issued or completed, and before any operations earlier than the barrier that meet the barrier condition have been issued.

## Usage Model

The IO Scheduler library is essentially a queue of IO operations combined with a thread pool to service them. The thread pool is designed to minimize context change latency and to service requests at the proper thread priority. A single worker thread can fetch a set of operations (“acquire”), feed them into the queue, pop the next prioritized op, and execute it (“issue”), and possibly release it, without requiring a context change. Further, other threads will concurrently attempt to acquire more ops, and issue in parallel if possible. These operations are accomplished by making callbacks to client. The callbacks prepare incoming operations (for example by servicing a channel on FIDL interface), handle the execution of the ops, and receive the completes result.

A client is expected to configure its interfaces then start the IO Scheduler. The Scheduler will then acquire, reorder, issue, and release operations through the client callbacks. The client creates the scheduler ops that are inserted into the scheduler and frees them after they are completed.

The client controls the ordering mechanics. Operations can be reordered within a stream. Operations are classified into three classes: Read, Write, and Barrier. Members of a class can be reordered with respect to each other and with respect to other classes, depending on the scheduler options provided. The Scheduler operates by basic reordering rules, but calls back into the client with each pair of ops to be reordered so that the client can determine if they have restrictions that would prevent them from being reordered. For example, a write should not be reordered ahead of a read when both target the same destination.

Synchronization is achieved using barriers. There are two types of barriers, Issue Barrier and Complete Barrier. An Issue Barrier guarantees that operations that match the barrier condition in a stream will be issued to the underlying driver stack prior to the barrier clearing. Note that they will not necessarily be ordered with respect to each other. Instructions earlier than the issue barrier will not issue until the barrier is cleared. A Complete Barrier will not clear until all matching earlier ops have both issued and completed. The operations need not complete successfully, they may have errors. Clients that depend on successful completion of an operation before scheduling others must wait on that completion before pushing new ops into the stream.

Synchronization between two or more streams is done using a combination of groups and barriers. A group containing barriers in multiple streams acts as a stream-synchronization barrier. The group does not complete until all barriers are ready to be cleared. This effectively blocks a stream (or a subset of its ops that match the barrier condition) until all streams addressed by the group have performed the required barrier.

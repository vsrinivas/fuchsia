# Zircon and LK

LK is a Kernel designed for small systems typically used in embedded
applications. It is a good alternative to commercial offerings like
[FreeRTOS](http://www.freertos.org/) or [ThreadX](http://rtos.com/products/threadx/).
Such systems often have a very limited amount of ram, a fixed set of peripherals
and a bounded set of tasks.

On the other hand, Zircon targets modern phones and modern personal computers
with fast processors, non-trivial amounts of ram with arbitrary peripherals
doing open ended computation.

Zircon inner constructs are based on [LK](https://github.com/littlekernel/lk) but
the layers above are new. For example, Zircon has the concept of a process but LK
does not. However, a Zircon process is made of by LK-level constructs such as
threads and memory.

More specifically, some the visible differences are:

+ Zircon has first class user-mode support. LK does not.
+ Zircon is an object-handle system. LK does not have either concept.
+ Zircon has a capability-based security model. In LK all code is trusted.

Over time, even the low level constructs will change to accomodate the new
requirements and to be a better fit with the rest of the system.


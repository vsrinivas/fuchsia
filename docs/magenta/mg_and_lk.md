# Magenta and LK

Magenta is based on [LK](https://github.com/littlekernel/lk) but over time it
will diverge from it significantly. As it does this document should be updated
accordingly.

LK is a Kernel designed for small systems typically used in embedded
applications. It is good alternative to commercial offerings like
[FreeRTOS](http://www.freertos.org/) or [ThreadX](http://rtos.com/products/threadx/).
Such systems often have a very limited amount of ram, a fixed set of peripherals
and a bounded set of tasks.

On the other hand, Magenta targets modern phones and modern personal computers
with fast processors, non-trivial amounts of ram with arbitrary peripherals
doing open ended computation.

More specifically, some the visible differences are:

+ Magenta has user-mode support `lib/lkuser, lib/magenta`, upstream LK does not.
+ Magenta has objects, they are manipulated by user mode via handles.
+ Magenta has a capability-based security model. In LK all code is trusted.
+ Magenta supports at the kernel level, the Mojo application model.

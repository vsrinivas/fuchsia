# Interrupt Event

## NAME

interrupt\_event - Usermode I/O interrupt delivery

## SYNOPSIS

Interrupt events allow userspace to create, signal, and wait on
hardware interrupts.

## DESCRIPTION

TODO

## NOTES

Interrupt Objects are private to the DDK and not generally available
to userspace processes.

## SYSCALLS

+ [interrupt_create](../syscalls/interrupt_create.md) - create an interrupt handle
+ [interrupt_wait](../syscalls/interrupt_wait.md) - wait for an interrupt on an interrupt handle
+ [interrupt_complete](../syscalls/interrupt_complete.md) - clear and unmask an interrupt handle
+ [interrupt_signal](../syscalls/interrupt_signal.md) - unblocks a wait on an interrupt handle

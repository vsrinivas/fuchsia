# Interrupts

## NAME

interrupts - Usermode I/O interrupt delivery

## SYNOPSIS

Interrupt objects allow userspace to create, signal, and wait on
hardware interrupts.

## DESCRIPTION

TODO

## NOTES

Interrupt Objects are private to the DDK and not generally available
to userspace processes.

## SYSCALLS

+ [interrupt_create](../syscalls/interrupt_create.md) - Create an interrupt handle
+ [interrupt_destroy](../syscalls/interrupt_destroy.md) - Destroy an interrupt handle
+ [interrupt_bind](../syscalls/interrupt_bind.md) - Bind an interrupt vector to interrupt handle
+ [interrupt_wait](../syscalls/interrupt_wait.md) - Wait for an interrupt on an interrupt handle
+ [interrupt_trigger](../syscalls/interrupt_trigger.md) - Triggers a virtual interrupt on an interrupt handle
+ [interrupt_ack](../syscalls/interrupt_ack.md) - Acknowledge an interrupt and re-arm it

# Log

## NAME

Log - Kernel debug log

## SYNOPSIS

Log objects allow userspace to read and write to kernel debug logs.

## DESCRIPTION

TODO

## NOTES

Log objects will likely cease being generally available to userspace
processes in the future.  They are intended for internal logging of
the kernel and device drivers.

## SYSCALLS

+ log_create - create a kernel managed log reader or writer
+ log_write - write log entry to log
+ log_read - read log entries from log

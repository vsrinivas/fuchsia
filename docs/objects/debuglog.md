# Log

## NAME

Debuglog - Kernel debuglog

## SYNOPSIS

Debuglog objects allow userspace to read and write to kernel debug logs.

## DESCRIPTION

TODO

## NOTES

Debuglog objects will likely cease being generally available to userspace
processes in the future.

## SYSCALLS

+ [debuglog_create](../syscalls/debuglog_create.md) - create a kernel managed debuglog reader or writer
+ [debuglog_write](../syscalls/debuglog_write.md) - write log entry to debuglog
+ [debuglog_read](../syscalls/debuglog_read.md) - read log entries from debuglog

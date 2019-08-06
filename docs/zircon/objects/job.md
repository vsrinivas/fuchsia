# Job

## NAME

job - Control a group of processes

## SYNOPSIS

A job is a group of processes and possibly other (child) jobs. Jobs are used to
track privileges to perform kernel operations (i.e., make various syscalls,
with various options), and track and limit basic resource (e.g., memory, CPU)
consumption. Every process belongs to a single job. Jobs can also be nested,
and every job except the root job also belongs to a single (parent) job.

## DESCRIPTION

A job is an object consisting of the following:
+ a reference to a parent job
+ a set of child jobs (each of whom has this job as parent)
+ a set of member [processes](process.md)
+ a set of policies [⚠ not implemented]

Jobs control "applications" that are composed of more than one process to be
controlled as a single entity.

## SYSCALLS

 - [`zx_job_create()`] - create a new child job.
 - [`zx_process_create()`] - create a new process within a job.
 - [`zx_job_set_policy()`] - set policy for new processes in the job.
 - [`zx_task_create_exception_channel()`] - listen for task exceptions
 - [`zx_task_kill()`] - cause a task to stop running.

[`zx_job_create()`]: ../syscalls/job_create.md
[`zx_job_set_policy()`]: ../syscalls/job_set_policy.md
[`zx_process_create()`]: ../syscalls/process_create.md
[`zx_task_create_exception_channel()`]: ../syscalls/task_create_exception_channel.md
[`zx_task_kill()`]: ../syscalls/task_kill.md

# `sched`

Simple tool for running a command with custom Zircon scheduler settings.

WARNING: The tool will only adjust the first thread in a new process: other
threads will just use the system default settings.

Example usage:

```sh
sched -p 31 /bin/my/command --command --line --args
```

## Full usage

```
sched [options] (-p <priority>) <cmd> [<args>...]

Apply scheduler parameters to the first thread of the given command.
Further spawned threads will run at the system default priority.

Options:
  -p <priority>       Run command at the given scheduler priority.
                      Valid priorities are 0 to 31, inclusive.

  -v                  Show verbose logging.
  --help              Show this help.
```

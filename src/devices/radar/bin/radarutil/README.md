radarutil
==========

The radarutil tool can be used to test the performance of radar drivers and
clients under different conditions.

Usage
----------

The radarutil tool takes the following arguments:

- Burst processing time (`-p`): The amount of time to sleep for each burst to
  simulate processing delay. Requires a suffix (`h`, `m`, `s`, `ms`, `us`, `ns`)
  indicating the units. Defaults to no delay.
- Run time (`-t`): The amount of time spend reading bursts before exiting.
  Requires a suffix indicating the units. Incompatible with the burst count
  option. Defaults to 1 second.
- Burst count (`-b`): The number of bursts to read before exiting. Incompatible
  with the run time option.
- VMO count (`-v`): The number of VMOs to register and use for reading bursts.
  Defaults to 10.

For example, to sleep 3 milliseconds for each burst, run for 5 minutes, and
register 20 VMOs, run: `radarutil -p 3ms -t 5m -v 20`

# Field Data

Ledger uses [Cobalt] to record field data.

In order to add more events:

 - edit Ledger code under [/src/cobalt]
 - register the new events, see documentation for [Cobalt client]

In order to view the recorded data, run:

```
cd garnet/bin/cobalt
./download_report_client.py
./report_client -report_master_uri=35.188.119.76:7001 -project_id=100
```

Then, follow `report_client` hints to extract the data. For example, run the
following to extract Ledger event counters from the last 5 days:

```
Command or 'help': run range -4 0 1
```

[Cobalt]: https://fuchsia.googlesource.com/cobalt
[/src/cobalt]: https://fuchsia.googlesource.com/ledger/+/master/src/cobalt/
[Cobalt client]: https://fuchsia.googlesource.com/cobalt_client

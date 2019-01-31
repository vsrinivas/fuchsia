# Field Data

Ledger uses [Cobalt] to record field data.

In order to add more events:

 - edit Ledger code under [/bin/ledger/cobalt]
 - register the new events, see documentation for [Cobalt client]

In order to view the recorded data, run:

```
cd garnet/bin/cobalt
./download_report_client.py
./report_client -project_id=100
```

Then, follow `report_client` hints to extract the data. For example, run the
following to extract Ledger event counters from the last 5 days:

```
Command or 'help': run range -4 0 2
```

[Cobalt]: https://fuchsia.googlesource.com/cobalt
[/bin/ledger/cobalt]: /bin/ledger/cobalt/
[Cobalt client]: https://fuchsia.googlesource.com/garnet/+/master/bin/cobalt/

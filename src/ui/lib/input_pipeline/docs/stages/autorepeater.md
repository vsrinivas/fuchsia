# input_pipeline > Autorepeater

Change the autorepeat settings using the following commands, for example:

```bash
fx shell run fuchsia-pkg://fuchsia.com/setui_client#meta/setui_client.cmx keyboard \
    --autorepeat-delay 500{s,second,seconds,ms,millisecond,milliseconds}
    --autorepeat-period 200{s,second,seconds,ms,millisecond,milliseconds}
```

Use:

```bash
fx shell run fuchsia-pkg://fuchsia.com/setui_client#meta/setui_client.cmx keyboard --help
```

for more information.


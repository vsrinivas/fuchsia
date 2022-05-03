# input_pipeline > Autorepeater

Change the autorepeat settings using the following commands, for example:

```bash
ffx config set setui true // only need to run once
ffx setui keyboard \
    --autorepeat-delay 500{s,second,seconds,ms,millisecond,milliseconds}
    --autorepeat-period 200{s,second,seconds,ms,millisecond,milliseconds}
```

Use:

```bash
ffx setui keyboard --help
```

for more information.


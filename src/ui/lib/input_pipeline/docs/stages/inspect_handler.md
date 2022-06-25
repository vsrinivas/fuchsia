# input_pipeline > Inspect handler

The inspect handler records useful metrics gleaned from processing the input
events. The metrics are exposed using Fuchsia's standard Inspect interfaces.

To see the recorded metrics use a command line such as this one:

```
fx ffx inspect show "core/ui/scene_manager"
```

The output will be like this:

```
core/ui/scene_manager:
  metadata:
    filename = fuchsia.inspect.Tree
    component_url = fuchsia-pkg://fuchsia.com/scene_manager#meta/scene_manager.cm
    timestamp = 62182195088
  payload:
    root:
      input_pipeline:
        input_pipeline_entry:
          events_count = 4
          last_generated_timestamp_ns = 36293071683
          last_seen_timestamp_ns = 36293838930
        input_pipeline_exit:
          events_count = 0
          last_generated_timestamp_ns = 0
          last_seen_timestamp_ns = 0
```

The interpretation of the metrics is as follows:

* `events_count`: The total number of `InputEvent`s that the stage has seen.
* `last_generated_timestamp_ns`: The timestamp of the last seen `InputEvent`, as
  propagated with the event.  Note that setting this timestamp is not yet very
  consistent so you may find that sometimes this is zero.
* `last_seen_timestamp_ns`: The timestamp at which the stage last saw an input
  event.  This can be compared to the `metadata.timestamp` to see how recently
  events have been processed.

You can insert however many inspect handlers you want, if your goal is to expose
metrics at multiple places in the input pipeline.  In the example above, the
input pipeline has been configured with two inspect handlers: one at the entry,
and one at the pipeline exit.  Normally the latter should see no events.

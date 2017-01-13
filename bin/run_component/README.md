Use `run_component` to launch applications packaged as components:
```
@ bootstrap run_component https://www.example.com/mycomponent
```

`run_component` creates a new environment containing all of the services in
its environment but adds a `modular::ApplicationLoader` that uses the
`component::ComponentIndex` to find and load component content locally or over
the network.

At some point this logic should move into another layer like `device_runner`.

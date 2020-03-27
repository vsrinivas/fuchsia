# `semantics manager tests`


To run the semantics tests

```
fx set core.<board> --with //bundles:tests --with-base //topaz/bundles:buildbot
fx build
fx reboot -r
fx run-test semantics_integration_tests
```

The semantics tests must be run on a product without a graphical base shell,
such as `core` because it starts and stop Scenic.

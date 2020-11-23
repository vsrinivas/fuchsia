# fidlc

The fidlc code lives in //zircon/tools/fidl. Once build unification is complete,
it can be moved here. In the meantime, this directory just contains the fidlc
goldens. To verify them:

```
fx test fidlc_golden_tests
```

To regenerate them:

```
fx regen-goldens fidlc
```

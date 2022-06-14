# package-directory compatibility tests

Integration tests for
[package-directory](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/lib/package-directory/).

To test a new implementation:
1. Add the implementation name to the
[harness fidl](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/tests/pkgdir/pkg-harness/test.test.fidl;l=9;drc=e3b39f2b57e720770773b857feca4f770ee0619e).
2. Have the harness return the new implementation when
[requested](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/tests/pkgdir/pkg-harness/src/main.rs;l=109;drc=41f3ab9d4aef3fa43cc488dae03e25d33078938b).
3. Have the tests request the new implementation
[from the harness](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/tests/pkgdir/src/lib.rs;l=31;drc=0e3c089f59bc075fa87459c65e72ed60130d2ad3).
4. If the new implementation has intentionally different behavior, add a method to
[PackageSource](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/tests/pkgdir/src/lib.rs;l=45;drc=0e3c089f59bc075fa87459c65e72ed60130d2ad3)
to query the implementation, then have the test
[branch](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/tests/pkgdir/src/directory.rs;l=141;drc=e443db01552d4a43f348033a63d54baf5e5cad4e)
on the implementation and document the difference in this README.

# Can play video test

To verify that the device under test can play video, `can_play_video_test` takes
a screenshot of the device, starts video, waits for 5 seconds, and takes another
screenshot to check if the screen is changed.

This test is currently not enabled in any products.

To be able to run this test for a product, add the test to the list of packages
built using the following command:

```posix-terminal
fx set <product>.<arch> --with src/tests/end_to_end/can_play_video:test
```

# link_update integration test

This test exercises the APIs to change data in a Link and registers notification
callbacks for when such changes occur.

While links are normally used by Modules, the Link API is also exposed to the
user shell, and this test is written as a user shell.

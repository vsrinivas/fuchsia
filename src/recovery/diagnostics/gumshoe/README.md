Gumshoe (On-Device Diagnostics)

How To Build

$ fx set core.sherlock \
  --auto-dir \
  --release \
  --with //src/recovery/diagnostics/gumshoe:tests \
  --with-base //src/recovery/diagnostics/gumshoe:gumshoe \
  --args='core_realm_shards +=
    [ "//src/recovery/diagnostics/gumshoe:gumshoe-core-shard" ]'


How To Test

After confirming your "fx set" includes this --with:

--with //src/recovery/diagnostics/gumshoe:tests

and confirming there's a "gumshoe-tests" section in your
OUT dir's "test.json" file, eg:

$ cat ~/fuchsia/out/core.sherlock-release/tests.json | grep name | grep gumshoe
      "name": "fuchsia-pkg://fuchsia.com/gumshoe-tests#meta/gumshoe-tests.cm",

You can run the tests using:

$ rm ~/output.txt; fx test -v gumshoe-tests --logpath ~/output.txt

and inspect ~/output.txt for any console output generated
by your tests.

An example test from handlebars_utils.rs is
"register_templates_generates_handlebars_calls()" and you
can force this test to fail by altering it:

@@ -79,7 +66,7 @@ mod tests {
             .times(3)
             .returning(|_,_| Ok(()));

-        register_templates(&mut mock_handlebars, vec!("1", "2", "3"))
+        register_templates(&mut mock_handlebars, vec!("1", "2", "3", "4"))
     }

and running the "fx test" command above, and then inspecting
~/output.txt for the error:

thread 'main' panicked at
'MockHandlebarsTrait::trait_register_template_file:
Expectation(<anything>) called more than 3 times'

How To Run Interactively (Harder)

To launch the Diagnostics component interactively, provided your
component is available on device (set using "--with-base") or from
your host (set using "-with" and running "fx serve"), this may
work:

$ ffx component run /core/ffx-laboratory:gumshoe fuchsia-pkg://fuchsia.com/gumshoe#meta/gumshoe.cm

depending on ffx-laboratory sandbox limitations. Look for limitations
by doing:

$ fx log | grep gumshoe

and looking for errors like:

WARNING: Required protocol `fuchsia.posix.socket.Provider` was not
available for target component `/core/ffx-laboratory:gumshoe`:
`/core/ffx-laboratory:gumshoe` tried to use `fuchsia.posix.socket.Provider`
from its parent, but the parent does not offer that capability.
Note, use clauses in CML default to using from parent.

You can get around these errors by editing what's exposed by
ffx-laboratory under:

~/fuchsia/src/developer/remote-control/meta/laboratory.core_shard.cml

but that can be challenging to maintain.

How To Run On Boot (Easier)

To launch the Diagnostics component when your device is booted,
add the Diagnostics component's core_shard manifest to the
core_realm_shards list of the product you're building.

This is done with the "fx set" argument:

 --args='core_realm_shards += \
   [ "//src/recovery/diagnostics/gumshoe:gumshoe-core-shard" ]'

which asks for some "gumshoe behavior" to be added to the
product's core shard, including "eagerly starting" gumshoe.

After making this change, build and reflash your device:

$ fx build
$ fx shell dm rb
$ fx flash

After your device is fully booted, verify the Diagnostics
component is running:

$ fx shell ps | grep gumshoe
    p: 29954               380.1K    380K     16K         gumshoe.cm

If it is not running, look in the logs for anything about
"not being able to find gumshoe", eg:

$ fx log | grep gumshoe
[00005.966601][2643][2645][base_resolver] ERROR: [../../src/sys/base-resolver/src/base_resolver.rs(77)] failed to resolve component URL fuchsia-pkg://fuchsia.com/gumshoe#meta/gumshoe.cm: package not found: open failed with status: NOT_FOUND: NOT_FOUND

In that case, maybe you just did "--with" instead of "--with-base"

After confirming the process is running, find your device's
IPV4 with:

$ ffx target list
NAME                    [...]  IP                                           RCS
fuchsia-f80f-f975-esf3  [...]  fe20::2bfb:1019:1a34:ac27%enxf82397511f2,    Y
                        [...]  172.16.243.141]

And try to hit the HTTP Server running on IPV4 on Port 8080.

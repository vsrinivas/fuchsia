# story_shell test

This test executes the story shell. Below is the sequence of actions and
verifications. All actions and verifications are driven by the user shell. The
story shell just responds.

* Create a story and start it.
* Add a top level module to the story. The module is specified by an intent. The
  intent is resolved to a module that possesses a manifest. The manifest
  specifies a composition pattern.
* We verify that the story shell receives the notification that the module has
  started, including the manifest.
* Stop the story. Start the story again.
* We verify that the story shell receives the same notification about the
  started module as before, including the manifest.

For each event we would like to verify, the story shell writes to TestStore
using Put(). The user shell uses Get() to register handlers for the keys it
expects the story shell to Put(), and continues when it has seen all the keys.

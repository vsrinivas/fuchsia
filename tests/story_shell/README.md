# story_shell test

This test executes the story shell. Below is the sequence of actions and
verifications. All actions and verifications are driven by the user shell. The
story shell just responds.

* Create a story and start it.
* Verify the contents of the link. The first time the story shell is created the
  link content is empty. If the content is empty, write to the link and verify
  those contents when the story shell is restarted.
* Add a top level module to the story. The module is specified by an intent. The
  intent is resolved to a module that possesses a manifest. The manifest
  specifies a composition pattern.
* We verify that the story shell receives the notification that the module has
  started, including the manifest.
* Stop the story. Start the story again.
* We verify that the story shell receives the same notification about the
  started module as before, including the manifest.

We also verify that the story receives the manifest regardless of how the intent
we use to add the module is resolved, i.e. regardless of whether it specifies
the action or the package of the module. For that, we run the whole sequence
above again, but use intents that specify handlers rather than actions to add
the modules.

For each event we would like to verify, the story shell writes to TestStore
using Put(). The user shell uses Get() to register handlers for the keys it
expects the story shell to Put(), and continues when it has seen all the keys.

# embed_shell integration test

This test executes the interaction of embedding modules with the story shell.

Modules can start other modules. They can do this either by *embedding* the view
of the other module into their own view, or by instructing the story runner to
send the view of the newly started module to the story shell. In that case, the
story shell is supplied with the view of the started module, an ID for it, the
ID of the view of the parent module, and the *surface relationship* of the new
module to its parent module. This information is used to layout the views of the
modules and provide navigation between them.

A subtlety arises when module one embeds module two, which starts module three,
but in the story shell. In that case, the story shell doesn't know about the
direct parent of module three, because it's embedded and its view is not sent to
the story shell. Instead, module one must be used as the display parent module
declared to the story shell for the view of module three.

This test executes that behavior. In this test, we use the following concrete
module implementations:

    * *one*: `embed_shell_test_parent_module`
    * *two*: `embed_shell_test_child_module`
    * *three*: `common_null_module`


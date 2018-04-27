# link_passing integration test

This tests the passing of links through modules.

1. Module1 starts Module2 and passes it a link.

2. Module2 starts Module3 and passed it the link it got from Module1.

3. Module3 sets a value in the link it got from Module1.

4. The test succeeds if Module1 sees the value in its link.

We do this for both a named link and a link with a null name (f.k.a. root link).

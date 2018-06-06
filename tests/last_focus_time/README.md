# last_focus_time integration test

The story runner records the time a story was last focused in the user's root
page as part of `fuchsia::modular::StoryInfo`.

This test verifies that requesting focus for a story indeed increases the
`last_focus_time` recorded for that story, and triggers the watchers for the
story.


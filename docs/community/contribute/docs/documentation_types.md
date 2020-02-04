# Documentation Types

Documentation is an important part of any product or feature because it lets users know how to
properly use a feature that has been implemented. These guidelines are meant to be a quick and easy
reference for types of documentation. For information on documentation style guidelines, see
[Documentation Style Guide](documentation_style_guide.md).

## Conceptual, procedural, or reference documentation

Most documentation can be divided into these categories:

- [Reference](#reference-documentation) - Documentation that provides a source of information about
  parts of a system such as API parameters.
- [Conceptual](#conceptual-documentation) - Documentation that helps you understand a concept such
  as mods in Fuchsia.
- [Procedural](#procedural-documentation)
    - How-to - Documentation that provides steps on how to accomplish a goal such as create a user.
    - Codelab - Documentation that provides steps of a learning path (this tends to be a much bigger
      procedure than a how-to) such as create a component.

**You should write a reference document** if you need to provide information about parts of a system
including, but not limited to APIs and CLIs. Reference documentation should allow the user to
understand how to use a specific feature quickly and easily.

**You should write a conceptual document** if you plan on explaining a concept about a product.
Conceptual documents explain a specific concept, but for the most part they do not include actual
examples. They provide essential facts, background, and diagrams to help your readers build a
foundational understanding of a product or topic. You should not explain industry standards that
your audience should be familiar with, for example, TCP/IP. You might explain how this concept ties
in with your feature, but you should not explain the basics behind that industry standard concept.

**You should write a procedural document** if you plan on explaining to a user how to use a specific
feature and are able to guide a user through simple numbered steps. Procedural documents tend to
reinforce the concepts that were explained in a conceptual document by giving one or more
examples that might be useful for users.

Procedural documents are divided into two categories:

- **How-to** - Consider writing a how to when you want to help the user accomplish a very specific
  goal.
- **Codelab** - Consider writing a codelab when you want to help the user learn about a bigger
  goal that might involve working with multiple parts of a product or feature. The codelab should not
  go over 60 minutes and should provide the user with a specific result.

How can you decide what type of document is appropriate for your use case? Consider these examples:

- What is a car? This is a conceptual document.
- How does an internal combustion engine work? This is a conceptual document that would be geared
  towards more advanced users.
- How to use the alarm manager in Android. That is a procedural document. The main set of
  procedures can be a codelab since a hand-held example is ideal to understand the function of the
  alarm manager.
- How to operate the radio. This is a procedural document. This can be a how to guide since the
  use of a radio tends to be quite intuitive and in most cases wouldn't require a hand-held example.
- How does a transistor work? This is a conceptual document that would be geared towards a more
  advanced user.
- Functions of the car radio. This is a reference document.
- How a new technology improved the car radio. This is a conceptual document.

Note: A feature may require more than one type of document. You may decide that your feature
requires just reference documentation or that you need reference, conceptual, and how to
documentation.

## Reference documentation {#reference-documentation}

Reference documentation should provide information about parts of a system including, but not
limited to APIs and CLIs. The style of reference documentation should be the same for all reference
documentation of that type. For example, API documentation should define all of the API's parameters,
indicate if a parameter is required or optional, and show examples of the use of the API. These
examples should be very generic and simple. If you feel like you need a more elaborate example,
consider creating a procedural document to reinforce your reference documentation.

For the style guide for API documentation, see
[API style guide](/docs/development/api/documentation.md).

## Conceptual documentation {#conceptual-documentation}

Conceptual documentation should try to be brief and for the most part should not go above 1 page.
If you need to write more than one page to describe a concept, consider breaking that concept into
sub-concepts by using headings. By keeping your document brief you achieve the following:

- You do not overwhelm your reader with a wall of text.
- Avoid losing the reader while they read your document.

The first paragraph should try to be a brief summary of your document, this should allow the user to
quickly read through it, determine what the document covers, and if this is relevant to what they
want to learn. If your document has multiple headings, you should include a bulleted list with the
high-level headings after this first paragraph.

You should use graphics, images, or diagrams to reinforce certain concepts. The text that comes
before and after the graphic should explain what the graphic shows. Images should be saved in
a feature specific 'images/' directory or a common 'images/' directory. You should also save
the source file of your images in a 'images/src/' directory.

Good conceptual documentation usually includes:

- **Description** rather than instruction
- **Background** concepts
- **Diagrams** or other visual aids (preferably in .png format)
- **Links** to how-to and/or reference docs

After writing your document, it is good practice to proofread the document, put yourself in the
user's shoes (no longer being the expert that developed the feature), and try to answer these
questions:

- Does the information in the document explain the concept completely?
- Is there information that is not needed for this concept? If so, remove it.
- Is there unnecessary detail about how things might work in the background?
- If I am the user, is there additional I would have liked to know?

Then, add your feedback into your document.

## Procedural documentation {#procedural-documentation}

Procedural documents are divided into two categories:

- **How-to** - Consider writing a how to when you want to help the user accomplish a very specific
  goal.
- **Codelab** - Consider writing a codelab when you want to help the user learn about a bigger goal
  that might involve working with multiple parts of a product or feature.

Procedural documentation should try to be brief and each task within your documentation should try
to avoid going above 10 steps (codelabs can be much longer, but should not exceed 45-60 minutes for
a user to complete). You should divide long procedures into multiple sub-tasks to try to keep tasks
manageable for a user. For example, if you wanted to write a procedural document for taking care of
a dog, you might have a table of content that looks like this:

How to take care of a dog:

- Feeding a dog
- Washing a dog
- Trimming a dog's nails
- Brushing a dog
- Playing with a dog

### Difference between a codelab and a how to

At a very high-level, a codelab is essentially a large how to, composed of various smaller how tos.
Codelabs are great when you want to give the user a hand-held experience of working through a task,
especially if this task is considered a little more complicated and might involve working with
various areas of a product. On the other hand, a how to should describe the steps on how to work
through a minor task that should only involve a single area of a product.

Consider the following when you think that you might need to create a codelab:

- How many codelabs are planned for this general feature? Keep in mind that you do not want a
  whole documentation set to just be codelabs, use them in moderation.
- Codelabs should be self-contained, avoid creating links to other codelabs, other how-tos or
  other information that might have a user leave the actual codelab. It is ok to provide links to
  conceptual documents that can enhance a user's knowledge for a given topic.
- Would this procedural documentation benefit from having a very specific example through a
  codelab?
- Do you want to expose an exciting feature from the product through the codelab? This helps you
  highlight a neat feature that a user might not know about without doing a codelab.

### General procedural documentation guidelines

- Each task or subtask should have a paragraph that lets a user know what the task is about and
  what a user should be able to do after performing the steps.
- Use screenshots or graphics to assist a user in navigating a user interface (UI).
- A procedural document should not have to explain any concepts to a user, but should reference
  conceptual documents in case a user does not know about a certain concept. For example, a
  procedure with a reference to a conceptual document might look like this:

   Configure the server with the appropriate configuration. For more information about server
   configurations, see "server configuration".

- Avoid giving the users multiple paths to select when working through procedures. When you avoid
  giving the user choices, your documentation should lead all users to the same end result (for
  example, starting the server).
- If a procedural document is meant for beginner users, avoid adding procedures that you might
  consider better suited for advanced users. If your document is intended for advanced users, state
  it up front and give them a list of prerequisites before they go through your how to or codelab.
  

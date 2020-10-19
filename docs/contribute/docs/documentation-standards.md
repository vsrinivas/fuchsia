# Documentation standards overview

This document provides high level standards and structure for documentation about
the Fuchsia operating system.

## Document locations

  * **Documentation specific to developing a specific Fuchsia feature:**
    Documentation for developers creating or maintaining a specific part of the Fuchsia codebase
    should be kept in the same directory as the source code. These docs are usually in the form of
    `README.md` files embedded throughout the Fuchsia codebase.
  * **General documentation for Fuchsia developers:** Fuchsia documentation should
    be created in `[/docs](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/)`.
    In the `/docs/` directory, you should create documentation in one of these sub-directories:

    * `[get-started](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/get-started/)`:
       Specific guidance to download, set up, and start developing on Fuchsia should go in
       `/docs/get-started`. This content should contain opinionated, short tutorials that help new
       users get started on Fuchsia, and link to additional documentation in Fuchsia.dev.
    *  `[development](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/)`:
        The `/docs/development/` directory (which displays on the site as "Guides") contains
        instructions and tutorials for developers
        working on Fuchsia. This directory includes documentation
        on how to build, run, and test Fuchsia.
    *  `[concepts](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/concepts/)`:
        The `/docs/concepts` directory contains in-depth explanations of specific features of
        Fuchsia and how they work, including operating system overviews, frameworks, architecture,
        and packages.
    *  `[reference](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/reference/)`:
        The `/docs/reference/` directory contains generated reference docs on Fuchsia tools and APIs,
        including FIDL and kernel reference.
    *  `[contribute](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/contribute/)`:
        The `/docs/contribute/` directory contains code and documentation contribution processes and
        best practices, including documentation code and style guides, code polcies, and governance.
    *  `images`
        The `/docs/images/` directory contains images used in the documentation. You should
        place images in this common directory.

## Document types

Most documentation can be divided into the following categories:

- [Procedural](documentation-types.md#procedural-documentation)
    - Getting started - Initial setup documentation
    - Guides - Task-oriented documentation
- [Conceptual](documentation-types.md#conceptual-documentation) - Foundational
  documentation focused on teaching more about Fuchsia, Fuchsia architecture, and Fuchsia components
- [Reference](documentation-types.md#reference-documentation) - Documentation focused on 
  detailing the syntax and parameters of Fuchsia APIs and tools. This documentation is usually
  auto-generated.

See [Documentation Types](documentation-types.md) for more information.

## Documentation and code style guides

It is important to try to follow documentation style guidelines to ensure that the documentation
created by a large number of contributors can flow together. See the
[Documentation style guide](documentation-style-guide.md) for specific documentation guidance and
[Code sample style guide](code-sample-style-guide.md) for code sample guidance.

## Search best practices

Documentation is only useful when users can find it. Some findability and search best practices
include the following:

- Add your document to the table of contents: Add links to documentation in the left sided
  navigation on fuchsia.dev. See [Site navigation and TOC files](documentation-navigation-toc.md)
  for more information.
- Cross-link documentation: Add links to documents on subjects that help readers better understand the
  content of your document. For example, the conceptual document for the [Fuchsia emulator](/docs/concepts/emulator/index.md)
  links to relevant guides and getting started documents about the Fuchsia emulator.
- Use consistent terminology: If you're writing about a specific concept in Fuchsia, verify that you are
  using consistent terminology. Use the [glossary](/docs/glossary.md) to verify terminology.

## Documentation file formats and file names

All documentation for Fuchsia is written in Markdown (`.md`), and Fuchsia.dev
uses the [Hoedown Markdown Parser](https://github.com/hoedown/hoedown).

The site's navigation is configured by `_toc.yaml` files, which are included in every documentation
directory. Use the guidance in
[Site navigation and TOC files](documentation-navigation-toc.md) to update these files.

File and directory names should be lowercase, and separate words with hyphens, not underscores.
Use only standard ASCII alphanumeric characters in file and directory names. If the file name
contains a command with an underscore, then you can include the underscore.

## General guidandce on style and tone

- **Write in plain U.S. English.** You should write in plain U.S. English and try to avoid over
  complicated words when you describe something. It's also ok to use contractions like "it's" or
  "don't".

- **Be respectful** Follow the guidelines set forth in [Respectful Code](/docs/contribute/best-practices/respectful_code.md).

- **Write in second-person ("you")**: When referring to the reader, write in second person using "you".
  For example, "You can install Fuchsia by doing the following...". Do not refer to the reader in the
  third person ("Fuchsia users can install Fuchsia by...") or use
  "We" ("We can install Fuchsia by...").

- **Keep sentences fairly short and concrete.** Using punctuation allows your reader to follow
  instructions or concepts. If by the time you read the last word of your sentence, you can't
  remember how the sentence started, it is probably too long. Also, short sentences are much easier
  to translate correctly.

- **Know your audience.** It is good practice to know your audience before you write documentation.
  Your audience can be, for example, developers, end-users, integrators, and they can have varying
  degrees of expertise and knowledge about a specific topic. Knowing your audience allows you to
  understand what information your audience should be familiar with. When a document is meant for a
  more advanced audience, it is best practice to state it up front and let the user know
  prerequisites before reading your document.

- **If you plan on using acronyms, you should define them the first time you write about them.** For
  example, looks good to me (LGTM). Don't assume that everyone will understand all acronyms. You do
  not need to define acronyms that might be considered industry standards such as TCP/IP.

- **Use active voice.** You should always try to write in the active voice since passive voice can
  make sentences very ambiguous and hard to understand. There are very few cases where you should
  use the passive voice for technical documentation.
  - Active voice - the subject performs the action denoted by the verb.
    - "The operating system runs a process." This sentence answers the question on what is
      happening and who/what is performing the action.
  - Passive voice - the subject is no longer _active_, but is, instead, being acted upon by the
    verb - or passive.
    - "A process is being run." This sentence is unclear about who or what is running the process.
      You might consider "a process is run by the operating system", but the object of the action
      is still made into the subject of the sentence which indicates passive voice. Passive voice
      tends to be wordier than active voice, which can make your sentence unclear. In most cases,
      if you use "by" this indicates that your sentence might be still be in passive voice. A
      correct way of writing this example is "The operating systems runs the process."


## Documentation pitfalls to avoid

- **Avoid using pronouns such as "I" or "we".** Fuchsia documentation is written to the user ("you").
  Using "I" or "we" in documentation is ambiguous and confusing to a reader. It's better to say
  "You should do…." instead of "We recommend that you do…." since this speaks directly to a user.

- **Avoid future tense.** Words such as "will" are very ambiguous. For example "you
  will see" can lead to questions such as "when will I see this?". In 1 minute or in 20 minutes? In
  most cases, assume that when someone reads the documentation you are sitting next to them and
  reading the instructions to them.

- **Do not list future plans for a product/feature.** "In the future, the product will have no
  bugs." This leads to the question as to when this would happen, but most importantly this is not
  something that anyone can guarantee will actually happen. Mentioning future plans that might not
  happen becomes a maintenence burden. Always document the system as it is, not as it will be.

- **Avoid using uncommon words, highly technical words, or jargon that users might not understand.**
  Avoid overcomplicating documentation with uncommon or highly technical words. If you're using
  technical jargon, make sure it's definied. If it's Fuchsia-specific documentation, you can point
  to the [glossary](/docs/glossary.md).

- **Avoid coloquial phrases or regional idioms**
  Also, avoid using idioms such as "that's the way the cookie crumbles", while it might make sense
  to you, it could not translate well into another language. Keep in mind that a lot of users are
  non-native English speakers.

- **Avoid referencing proprietary information.** This can refer to any potential terminology or
  product names that may be trademarked or any internal information (API keys, machine names, etc…).




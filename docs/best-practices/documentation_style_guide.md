# General Style guidelines for all documentation

It is important to create documentation that follows similar guidelines. This allows documentation
to be clear and concise while allowing users to easily find necessary information. For information
about the complete documentation standards, see
[Documentation Standards](documentation_standards.md).

These are general style guidelines that can help create clearer documentation:

- **Avoid using pronouns such as "I" or "we".** These can be quite ambiguous when someone reads the
  documentation. It is better to say "You should do…." instead of "We recommend that you do….". It
  is ok to use "you" as this allows the documentation to speak to a user.
- **If you plan on using acronyms, you should define them the first time you write about them.** For
  example, looks good to me (LGTM). Don't assume that everyone will understand all acronyms.
- **In most cases, avoid future tense.** Words such as "will" are very ambiguous. For example "you
  will see" can lead to questions such as "when will I see this?". In 1 minute or in 20 minutes? In
  most cases, assume that when someone reads the documentation you are sitting next to them and
  reading the instructions to them.
- **Avoid the use of passive voice and use active voice.** Passive voice can make sentences very
  ambiguous and hard to understand. There are very few cases where you should use the passive voice
  for technical documentation.
  - Active voice - the subject performs the action denoted by the verb.
    - "The operating system runs a process." This sentence answers the question on what is
      happening and who/what is performing the action.
  - Passive voice - the subject is no longer _active_, but is, instead, being acted upon by the
    verb - or passive.
    - "A process is being run." This sentence is unclear about who or what is running the process.
- **Do not list future plans for a product/feature.** "In the future, the product will have no
  bugs." This leads to the question as to when this would happen, but most importantly this is not
  something that anyone can guarantee will actually happen.
- **Do not talk about how certain features work behind the covers unless it is absolutely necessary.**
  Always ask yourself, "Is this text necessary to understand this concept or to get through these
  instructions?" This also leads to shorter (less maintenance) and more concise (happier readers)
  documentation.
- **Avoid using uncommon words, highly technical words, or jargon that users might not understand.**
  Also, avoid using idioms such as "that's the way the cookie crumbles", while it might make sense
  to you, it could not translate well into another language. Keep in mind that a lot of users are
  non-native English speakers.
- **Avoid using words such as "best" or "great" since these are all relative terms.** How can you
  prove that "this operating system is the best?"
- **Avoid referencing proprietary information.** This can refer to any potential terminology or
  product names that may be trademarked or any internal information (API keys, machine names, etc…)
- **Avoid starting a sentence with "this" since it is unclear what "this" references.**
  - For example: "The operating system is fast and efficient. This is what makes it well designed."
    Does "this" refer to fast, efficient, or operating system? Consider using: "The operating system
    is well designed because it is fast and efficient".
- **Keep sentences fairly short and concrete.** Using punctuation allows your reader to follow
  instructions or concepts. If, by the time you read the last word of your sentence, you can't
  remember how the sentence started, it is probably too long. Also, short sentences are much easier
  to translate correctly.
- **Let users know if your documentation is meant for advanced users.** When a document is meant for
  a more advanced audience, it is best practice to state it up front and let the user know
  prerequisites before reading your document.

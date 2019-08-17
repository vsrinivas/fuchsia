//! Lorem ipsum generator.
//!
//! This crate contains functions for generating pseudo-Latin lorem
//! ipsum placeholder text. The traditional lorem ipsum text start
//! like this:
//!
//! > Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do
//! > eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut
//! > enim ad minim veniam, quis nostrud exercitation ullamco laboris
//! > nisi ut aliquip ex ea commodo consequat. [...]
//!
//! This text is in the [`LOREM_IPSUM`] constant. Random text looking
//! like the above can be generated using the [`lipsum`] function.
//! This function allows you to generate as much text as desired and
//! each invocation will generate different text. This is done using a
//! [Markov chain] based on both the [`LOREM_IPSUM`] and
//! [`LIBER_PRIMUS`] texts. The latter constant holds the full text of
//! the first book of a work by Cicero, of which the lorem ipsum text
//! is a scrambled subset.
//!
//! The random looking text is generatd using a Markov chain of order
//! two, which simply means that the next word is based on the
//! previous two words in the input texts. The Markov chain can be
//! used with other input texts by creating an instance of
//! [`MarkovChain`] and calling its [`learn`] method.
//!
//! [`LOREM_IPSUM`]: constant.LOREM_IPSUM.html
//! [`LIBER_PRIMUS`]: constant.LIBER_PRIMUS.html
//! [`lipsum`]: fn.lipsum.html
//! [`MarkovChain`]: struct.MarkovChain.html
//! [`learn`]: struct.MarkovChain.html#method.learn
//! [Markov chain]: https://en.wikipedia.org/wiki/Markov_chain

#![doc(html_root_url = "https://docs.rs/lipsum/0.6.0")]
#![deny(missing_docs)]

extern crate rand;
#[cfg(test)]
extern crate rand_xorshift;

use rand::rngs::ThreadRng;
use rand::seq::SliceRandom;
use rand::Rng;
use std::cell::RefCell;
use std::collections::HashMap;

/// A bigram is simply two consecutive words.
pub type Bigram<'a> = (&'a str, &'a str);

/// Simple order two Markov chain implementation.
///
/// The [Markov chain] is a chain of order two, which means that it
/// will use the previous two words (a bigram) when predicting the
/// next word. This is normally enough to generate random text that
/// looks somewhat plausible. The implementation is based on
/// [Generating arbitrary text with Markov chains in Rust][blog post].
///
/// [Markov chain]: https://en.wikipedia.org/wiki/Markov_chain
/// [blog post]: https://blakewilliams.me/posts/generating-arbitrary-text-with-markov-chains-in-rust
pub struct MarkovChain<'a, R: Rng> {
    map: HashMap<Bigram<'a>, Vec<&'a str>>,
    keys: Vec<Bigram<'a>>,
    rng: R,
}

impl<'a> MarkovChain<'a, ThreadRng> {
    /// Create a new empty Markov chain. It will use a default
    /// thread-local random number generator.
    ///
    /// # Examples
    ///
    /// ```
    /// use lipsum::MarkovChain;
    ///
    /// let chain = MarkovChain::new();
    /// assert!(chain.is_empty());
    /// ```
    pub fn new() -> MarkovChain<'a, ThreadRng> {
        MarkovChain::new_with_rng(rand::thread_rng())
    }
}

impl<'a> Default for MarkovChain<'a, ThreadRng> {
    /// Create a new empty Markov chain. It will use a default
    /// thread-local random number generator.
    fn default() -> Self {
        Self::new()
    }
}

impl<'a, R: Rng> MarkovChain<'a, R> {
    /// Create a new empty Markov chain that uses the given random
    /// number generator.
    ///
    /// # Examples
    ///
    /// ```
    /// extern crate rand;
    /// extern crate rand_xorshift;
    /// # extern crate lipsum;
    ///
    /// # fn main() {
    /// use rand::SeedableRng;
    /// use rand_xorshift::XorShiftRng;
    /// use lipsum::MarkovChain;
    ///
    /// let rng = XorShiftRng::seed_from_u64(0);
    /// let mut chain = MarkovChain::new_with_rng(rng);
    /// chain.learn("infra-red red orange yellow green blue indigo x-ray");
    ///
    /// // The chain jumps consistently like this:
    /// assert_eq!(chain.generate(1), "Yellow.");
    /// assert_eq!(chain.generate(1), "Blue.");
    /// assert_eq!(chain.generate(1), "Green.");
    /// # }
    /// ```

    pub fn new_with_rng(rng: R) -> MarkovChain<'a, R> {
        MarkovChain {
            map: HashMap::new(),
            keys: Vec::new(),
            rng: rng,
        }
    }

    /// Add new text to the Markov chain. This can be called several
    /// times to build up the chain.
    ///
    /// # Examples
    ///
    /// ```
    /// use lipsum::MarkovChain;
    ///
    /// let mut chain = MarkovChain::new();
    /// chain.learn("red green blue");
    /// assert_eq!(chain.words(("red", "green")), Some(&vec!["blue"]));
    ///
    /// chain.learn("red green yellow");
    /// assert_eq!(chain.words(("red", "green")), Some(&vec!["blue", "yellow"]));
    /// ```
    pub fn learn(&mut self, sentence: &'a str) {
        let words = sentence.split_whitespace().collect::<Vec<&str>>();
        for window in words.windows(3) {
            let (a, b, c) = (window[0], window[1], window[2]);
            self.map.entry((a, b)).or_insert_with(Vec::new).push(c);
        }
        // Sync the keys with the current map.
        self.keys = self.map.keys().cloned().collect();
        self.keys.sort();
    }

    /// Returs the number of states in the Markov chain.
    ///
    /// # Examples
    ///
    /// ```
    /// use lipsum::MarkovChain;
    ///
    /// let mut chain = MarkovChain::new();
    /// assert_eq!(chain.len(), 0);
    ///
    /// chain.learn("red orange yellow green blue indigo");
    /// assert_eq!(chain.len(), 4);
    /// ```
    #[inline]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    /// Returns `true` if the Markov chain has no states.
    ///
    /// # Examples
    ///
    /// ```
    /// use lipsum::MarkovChain;
    ///
    /// let mut chain = MarkovChain::new();
    /// assert!(chain.is_empty());
    ///
    /// chain.learn("foo bar baz");
    /// assert!(!chain.is_empty());
    /// ```
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Get the possible words following the given bigram, or `None`
    /// if the state is invalid.
    ///
    /// # Examples
    ///
    /// ```
    /// use lipsum::MarkovChain;
    ///
    /// let mut chain = MarkovChain::new();
    /// chain.learn("red green blue");
    /// assert_eq!(chain.words(("red", "green")), Some(&vec!["blue"]));
    /// assert_eq!(chain.words(("foo", "bar")), None);
    /// ```
    pub fn words(&self, state: Bigram<'a>) -> Option<&Vec<&str>> {
        self.map.get(&state)
    }

    /// Generate a sentence with `n` words of lorem ipsum text. The
    /// sentence will start from a random point in the Markov chain
    /// and a `.` will be added as necessary to form a full sentence.
    ///
    /// See [`generate_from`] if you want to control the starting
    /// point for the generated text and see [`iter`] if you simply
    /// want a sequence of words.
    ///
    /// # Examples
    ///
    /// Generating the sounds of a grandfather clock:
    ///
    /// ```
    /// use lipsum::MarkovChain;
    ///
    /// let mut chain = MarkovChain::new();
    /// chain.learn("Tick, Tock, Tick, Tock, Ding! Tick, Tock, Ding! Ding!");
    /// println!("{}", chain.generate(15));
    /// ```
    ///
    /// The output looks like this:
    ///
    /// > Ding! Tick, Tock, Tick, Tock, Ding! Ding! Tock, Ding! Tick,
    /// > Tock, Tick, Tock, Tick, Tock.
    ///
    /// [`generate_from`]: struct.MarkovChain.html#method.generate_from
    /// [`iter`]: struct.MarkovChain.html#method.iter
    pub fn generate(&mut self, n: usize) -> String {
        join_words(self.iter().take(n))
    }

    /// Generate a sentence with `n` words of lorem ipsum text. The
    /// sentence will start from the given bigram and a `.` will be
    /// added as necessary to form a full sentence.
    ///
    /// Use [`generate`] if the starting point is not important. See
    /// [`iter_from`] if you want a sequence of words that you can
    /// format yourself.
    ///
    /// [`generate`]: struct.MarkovChain.html#method.generate
    /// [`iter_from`]: struct.MarkovChain.html#method.iter_from
    pub fn generate_from(&mut self, n: usize, from: Bigram<'a>) -> String {
        join_words(self.iter_from(from).take(n))
    }

    /// Make a never-ending iterator over the words in the Markov
    /// chain. The iterator starts at a random point in the chain.
    pub fn iter(&mut self) -> Words<R> {
        let state = if self.is_empty() {
            ("", "")
        } else {
            *self.keys.choose(&mut self.rng).unwrap()
        };
        Words {
            map: &self.map,
            rng: &mut self.rng,
            keys: &self.keys,
            state: state,
        }
    }

    /// Make a never-ending iterator over the words in the Markov
    /// chain. The iterator starts at the given bigram.
    pub fn iter_from(&mut self, from: Bigram<'a>) -> Words<R> {
        Words {
            map: &self.map,
            rng: &mut self.rng,
            keys: &self.keys,
            state: from,
        }
    }
}

/// Never-ending iterator over words in the Markov chain.
///
/// Generated with the [`iter`] or [`iter_from`] methods.
///
/// [`iter`]: struct.MarkovChain.html#method.iter
/// [`iter_from`]: struct.MarkovChain.html#method.iter_from
pub struct Words<'a, R: 'a + Rng> {
    map: &'a HashMap<Bigram<'a>, Vec<&'a str>>,
    rng: &'a mut R,
    keys: &'a Vec<Bigram<'a>>,
    state: Bigram<'a>,
}

impl<'a, R: Rng> Iterator for Words<'a, R> {
    type Item = &'a str;

    fn next(&mut self) -> Option<&'a str> {
        if self.map.is_empty() {
            return None;
        }

        let result = Some(self.state.0);

        while !self.map.contains_key(&self.state) {
            self.state = *self.keys.choose(self.rng).unwrap();
        }
        let next_words = &self.map[&self.state];
        let next = next_words.choose(self.rng).unwrap();
        self.state = (self.state.1, next);
        result
    }
}

/// Check if `c` is an ASCII punctuation character.
fn is_ascii_punctuation(c: char) -> bool {
    // We use the table from the unstable
    // AsciiExt::is_ascii_punctuation function:
    //
    // U+0021 ... U+002F `! " # $ % & ' ( ) * + , - . /`
    // U+003A ... U+0040 `: ; < = > ? @`
    // U+005B ... U+0060 `[ \\ ] ^ _ \``
    // U+007B ... U+007E `{ | } ~`
    match c {
        '\x21'...'\x2F' | '\x3A'...'\x40' | '\x5B'...'\x60' | '\x7B'...'\x7E' => true,
        _ => false,
    }
}

/// Capitalize the first character in a string.
fn capitalize<'a>(word: &'a str) -> String {
    let idx = match word.chars().next() {
        Some(c) => c.len_utf8(),
        None => 0,
    };

    let mut result = String::with_capacity(word.len());
    result.push_str(&word[..idx].to_uppercase());
    result.push_str(&word[idx..]);
    result
}

/// Join words from an iterator. The first word is always capitalized
/// and the generated sentence will end with `'.'` if it doesn't
/// already end with some other ASCII punctuation character.
fn join_words<'a, I: Iterator<Item = &'a str>>(mut words: I) -> String {
    match words.next() {
        None => String::new(),
        Some(word) => {
            let mut sentence = capitalize(word);

            // Add remaining words.
            for word in words {
                sentence.push(' ');
                sentence.push_str(word);
            }

            // Ensure the sentence ends with either one of ".!?".
            if !sentence.ends_with(|c: char| c == '.' || c == '!' || c == '?') {
                // Trim all trailing punctuation characters to avoid
                // adding '.' after a ',' or similar.
                let idx = sentence.trim_right_matches(is_ascii_punctuation).len();
                sentence.truncate(idx);
                sentence.push('.');
            }

            sentence
        }
    }
}

/// The traditional lorem ipsum text as given in [Wikipedia]. Using
/// this text alone for a Markov chain of order two doesn't work very
/// well since each bigram (two consequtive words) is followed by just
/// one other word. In other words, the Markov chain will always
/// produce the same output and recreate the lorem ipsum text
/// precisely. However, combining it with the full text in
/// [`LIBER_PRIMUS`] works well.
///
/// [Wikipedia]: https://en.wikipedia.org/wiki/Lorem_ipsum
/// [`LIBER_PRIMUS`]: constant.LIBER_PRIMUS.html
pub const LOREM_IPSUM: &'static str = include_str!("lorem-ipsum.txt");

/// The first book in Cicero's work De finibus bonorum et malorum ("On
/// the ends of good and evil"). The lorem ipsum text in
/// [`LOREM_IPSUM`] is derived from part of this text.
///
/// [`LOREM_IPSUM`]: constant.LOREM_IPSUM.html
pub const LIBER_PRIMUS: &'static str = include_str!("liber-primus.txt");

thread_local! {
    // Markov chain generating lorem ipsum text.
    static LOREM_IPSUM_CHAIN: RefCell<MarkovChain<'static, ThreadRng>> = {
        let mut chain = MarkovChain::new();
        // The cost of learning increases as more and more text is
        // added, so we start with the smallest text.
        chain.learn(LOREM_IPSUM);
        chain.learn(LIBER_PRIMUS);
        RefCell::new(chain)
    }
}

/// Generate `n` words of lorem ipsum text. The output will always
/// start with "Lorem ipsum".
///
/// The text continues with the standard lorem ipsum text from
/// [`LOREM_IPSUM`] and becomes random if more than 18 words is
/// requested. See [`lipsum_words`] if fully random text is needed.
///
/// # Examples
///
/// ```
/// use lipsum::lipsum;
///
/// assert_eq!(lipsum(7), "Lorem ipsum dolor sit amet, consectetur adipiscing.");
/// ```
///
/// [`LOREM_IPSUM`]: constant.LOREM_IPSUM.html
/// [`lipsum_words`]: fn.lipsum_words.html
pub fn lipsum(n: usize) -> String {
    LOREM_IPSUM_CHAIN.with(|cell| {
        let mut chain = cell.borrow_mut();
        chain.generate_from(n, ("Lorem", "ipsum"))
    })
}

/// Generate `n` words of random lorem ipsum text.
///
/// The text starts with a random word from [`LOREM_IPSUM`]. Multiple
/// sentences may be generated, depending on the punctuation of the
/// words being random selected.
///
/// # Examples
///
/// ```
/// use lipsum::lipsum_words;
///
/// println!("{}", lipsum_words(6));
/// // -> "Propter soliditatem, censet in infinito inani."
/// ```
///
/// [`LOREM_IPSUM`]: constant.LOREM_IPSUM.html
pub fn lipsum_words(n: usize) -> String {
    LOREM_IPSUM_CHAIN.with(|cell| {
        let mut chain = cell.borrow_mut();
        chain.generate(n)
    })
}

/// Minimum number of words to include in a title.
const TITLE_MIN_WORDS: usize = 3;
/// Maximum number of words to include in a title.
const TITLE_MAX_WORDS: usize = 8;
/// Words shorter than this size are not capitalized.
const TITLE_SMALL_WORD: usize = 3;

/// Generate a short lorem ipsum text with words in title case.
///
/// The words are capitalized and stripped for punctuation characters.
///
/// # Examples
///
/// ```
/// use lipsum::lipsum_title;
///
/// println!("{}", lipsum_title());
/// ```
///
/// This will generate a string like
///
/// > Grate Meminit et Praesentibus
///
/// which should be suitable for use in a document title for section
/// heading.
pub fn lipsum_title() -> String {
    LOREM_IPSUM_CHAIN.with(|cell| {
        let n = rand::thread_rng().gen_range(TITLE_MIN_WORDS, TITLE_MAX_WORDS);
        let mut chain = cell.borrow_mut();
        // The average word length with our corpus is 7.6 bytes so
        // this capacity will avoid most allocations.
        let mut title = String::with_capacity(8 * n);

        let words = chain
            .iter()
            .map(|word| word.trim_matches(is_ascii_punctuation))
            .filter(|word| !word.is_empty())
            .take(n);

        for (i, word) in words.enumerate() {
            if i > 0 {
                title.push(' ');
            }

            // Capitalize the first word and all long words.
            if i == 0 || word.len() > TITLE_SMALL_WORD {
                title.push_str(&capitalize(word));
            } else {
                title.push_str(word);
            }
        }
        title
    })
}

#[cfg(test)]
mod tests {
    use super::rand::SeedableRng;
    use super::rand_xorshift::XorShiftRng;
    use super::*;

    #[test]
    fn starts_with_lorem_ipsum() {
        assert_eq!(&lipsum(10)[..11], "Lorem ipsum");
    }

    #[test]
    fn generate_zero_words() {
        assert_eq!(lipsum(0).split_whitespace().count(), 0);
    }

    #[test]
    fn generate_one_word() {
        assert_eq!(lipsum(1).split_whitespace().count(), 1);
    }

    #[test]
    fn generate_two_words() {
        assert_eq!(lipsum(2).split_whitespace().count(), 2);
    }

    #[test]
    fn starts_differently() {
        // Check that calls to lipsum_words don't always start with
        // "Lorem ipsum".
        let idx = "Lorem ipsum".len();
        assert_ne!(&lipsum_words(5)[..idx], &lipsum_words(5)[..idx]);
    }

    #[test]
    fn generate_title() {
        for word in lipsum_title().split_whitespace() {
            assert!(
                !word.starts_with(is_ascii_punctuation) && !word.ends_with(is_ascii_punctuation),
                "Unexpected punctuation: {:?}",
                word
            );
            if word.len() > TITLE_SMALL_WORD {
                assert!(
                    word.starts_with(char::is_uppercase),
                    "Expected small word to be capitalized: {:?}",
                    word
                );
            }
        }
    }

    #[test]
    fn empty_chain() {
        let mut chain = MarkovChain::new();
        assert_eq!(chain.generate(10), "");
    }

    #[test]
    fn generate_from() {
        let mut chain = MarkovChain::new();
        chain.learn("red orange yellow green blue indigo violet");
        assert_eq!(
            chain.generate_from(5, ("orange", "yellow")),
            "Orange yellow green blue indigo."
        );
    }

    #[test]
    fn generate_last_bigram() {
        // The bigram "yyy zzz" will not be present in the Markov
        // chain's map, and so we will not generate "xxx yyy zzz" as
        // one would expect. The chain moves from state "xxx yyy" to
        // "yyy zzz", but sees that as invalid state and resets itself
        // back to "xxx yyy".
        let mut chain = MarkovChain::new();
        chain.learn("xxx yyy zzz");
        assert_ne!(chain.generate_from(3, ("xxx", "yyy")), "xxx yyy zzz");
    }

    #[test]
    fn generate_from_no_panic() {
        // No panic when asked to generate a chain from a starting
        // point that doesn't exist in the chain.
        let mut chain = MarkovChain::new();
        chain.learn("foo bar baz");
        chain.generate_from(3, ("xxx", "yyy"));
    }

    #[test]
    fn chain_map() {
        let mut chain = MarkovChain::new();
        chain.learn("foo bar baz quuz");
        let map = &chain.map;

        assert_eq!(map.len(), 2);
        assert_eq!(map[&("foo", "bar")], vec!["baz"]);
        assert_eq!(map[&("bar", "baz")], vec!["quuz"]);
    }

    #[test]
    fn new_with_rng() {
        let rng = XorShiftRng::seed_from_u64(1234);
        let mut chain = MarkovChain::new_with_rng(rng);
        chain.learn("foo bar x y z");
        chain.learn("foo bar a b c");

        assert_eq!(chain.generate(15), "A b x y y b y bar a b y x y bar a.");
    }
}

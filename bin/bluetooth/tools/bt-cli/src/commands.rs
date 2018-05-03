use rustyline::completion::Completer;
use rustyline::error::ReadlineError;
use std::fmt;
use std::str::FromStr;

macro_rules! gen_completer {
    ($name:ident {
        $($variant:ident = ($val:expr, $help:expr)),*,
    }) => {
        #[derive(PartialEq)]
        pub enum $name {
            $($variant),*
        }

        impl $name {
            pub fn variants() -> Vec<String> {
                let mut variants = Vec::new();
                $(variants.push($val.to_string());)*
                variants
            }

            pub fn help_msg() -> String {
                let mut msg = String::new();
                $(
                    msg.push_str(format!("{} -- {}\n", $val, $help).as_str());
                )*
                msg
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
                match *self {
                    $($name::$variant => write!(f, $val)),* ,
                }
            }
        }

        impl FromStr for $name {
            type Err = ();

            fn from_str(s: &str) -> Result<$name, ()> {
                match s {
                    $($val => Ok($name::$variant)),* ,
                    _ => Err(()),
                }
            }
        }

    }
}

gen_completer! {
    Cmd {
        ActiveAdapter = ("adapter", "Show the Active Adapter"),
        Help = ("help", "This message"),
        GetAdapters = ("list-adapters", "Show all known bluetooth adapters"),
        StartDiscovery = ("start-discovery", "Start Discovery"),
        StopDiscovery = ("stop-discovery", "Stop Discovery"),
    }
}

pub struct CmdCompleter;

impl CmdCompleter {
    pub fn new() -> CmdCompleter {
        CmdCompleter {}
    }
}

impl Completer for CmdCompleter {
    fn complete(&self, line: &str, _pos: usize) -> Result<(usize, Vec<String>), ReadlineError> {
        let mut variants = Vec::new();
        for variant in Cmd::variants() {
            if variant.contains(line) {
                variants.push(variant)
            }
        }
        Ok((0, variants))
    }
}

use crate::JsonValue;
use std::collections::HashMap;
use std::fmt;
use std::io::{self, Write};

#[derive(Debug)]
pub struct JsonGenerateError {
    msg: String,
}

impl JsonGenerateError {
    pub fn message(&self) -> &str {
        self.msg.as_str()
    }
}

impl fmt::Display for JsonGenerateError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Generate error: {}", &self.msg)
    }
}

impl std::error::Error for JsonGenerateError {}

pub type JsonGenerateResult = Result<String, JsonGenerateError>;

pub struct JsonGenerator<'indent, W: Write> {
    out: W,
    indent: Option<&'indent str>,
}

impl<'indent, W: Write> JsonGenerator<'indent, W> {
    pub fn new(out: W) -> Self {
        Self { out, indent: None }
    }

    pub fn indent(mut self, indent: &'indent str) -> Self {
        self.indent = Some(indent);
        self
    }

    fn quote(&mut self, s: &str) -> io::Result<()> {
        self.out.write_all(b"\"")?;
        for c in s.chars() {
            match c {
                '\\' => self.out.write_all(b"\\\\")?,
                '\u{0008}' => self.out.write_all(b"\\b")?,
                '\u{000c}' => self.out.write_all(b"\\f")?,
                '\n' => self.out.write_all(b"\\n")?,
                '\r' => self.out.write_all(b"\\r")?,
                '\t' => self.out.write_all(b"\\t")?,
                '"' => self.out.write_all(b"\\\"")?,
                c if c.is_control() => write!(self.out, "\\u{:04x}", c as u32)?,
                c => write!(self.out, "{}", c)?,
            }
        }
        self.out.write_all(b"\"")
    }

    fn number(&mut self, f: f64) -> io::Result<()> {
        if f.is_infinite() {
            Err(io::Error::new(
                io::ErrorKind::Other,
                "JSON cannot represent inf",
            ))
        } else if f.is_nan() {
            Err(io::Error::new(
                io::ErrorKind::Other,
                "JSON cannot represent NaN",
            ))
        } else {
            write!(self.out, "{}", f)
        }
    }

    fn encode_array(&mut self, array: &[JsonValue]) -> io::Result<()> {
        self.out.write_all(b"[")?;
        let mut first = true;
        for elem in array.iter() {
            if first {
                first = false;
            } else {
                self.out.write_all(b",")?;
            }
            self.encode(elem)?;
        }
        self.out.write_all(b"]")
    }

    fn encode_object(&mut self, m: &HashMap<String, JsonValue>) -> io::Result<()> {
        self.out.write_all(b"{")?;
        let mut first = true;
        for (k, v) in m {
            if first {
                first = false;
            } else {
                self.out.write_all(b",")?;
            }
            self.quote(k)?;
            self.out.write_all(b":")?;
            self.encode(v)?;
        }
        self.out.write_all(b"}")
    }

    fn encode(&mut self, value: &JsonValue) -> io::Result<()> {
        match value {
            JsonValue::Number(n) => self.number(*n),
            JsonValue::Boolean(b) => write!(self.out, "{}", *b),
            JsonValue::String(s) => self.quote(s),
            JsonValue::Null => self.out.write_all(b"null"),
            JsonValue::Array(a) => self.encode_array(a),
            JsonValue::Object(o) => self.encode_object(o),
        }
    }

    fn write_indent(&mut self, indent: &str, level: usize) -> io::Result<()> {
        for _ in 0..level {
            self.out.write_all(indent.as_bytes())?;
        }
        Ok(())
    }

    fn format_array(&mut self, array: &[JsonValue], indent: &str, level: usize) -> io::Result<()> {
        if array.is_empty() {
            return self.out.write_all(b"[]");
        }

        self.out.write_all(b"[\n")?;
        let mut first = true;
        for elem in array.iter() {
            if first {
                first = false;
            } else {
                self.out.write_all(b",\n")?;
            }
            self.write_indent(indent, level + 1)?;
            self.format(elem, indent, level + 1)?;
        }
        self.out.write_all(b"\n")?;
        self.write_indent(indent, level)?;
        self.out.write_all(b"]")
    }

    fn format_object(
        &mut self,
        m: &HashMap<String, JsonValue>,
        indent: &str,
        level: usize,
    ) -> io::Result<()> {
        if m.is_empty() {
            return self.out.write_all(b"{}");
        }

        self.out.write_all(b"{\n")?;
        let mut first = true;
        for (k, v) in m {
            if first {
                first = false;
            } else {
                self.out.write_all(b",\n")?;
            }
            self.write_indent(indent, level + 1)?;
            self.quote(k)?;
            self.out.write_all(b": ")?;
            self.format(v, indent, level + 1)?;
        }
        self.out.write_all(b"\n")?;
        self.write_indent(indent, level)?;
        self.out.write_all(b"}")
    }

    fn format(&mut self, value: &JsonValue, indent: &str, level: usize) -> io::Result<()> {
        match value {
            JsonValue::Number(n) => self.number(*n),
            JsonValue::Boolean(b) => write!(self.out, "{}", *b),
            JsonValue::String(s) => self.quote(s),
            JsonValue::Null => self.out.write_all(b"null"),
            JsonValue::Array(a) => self.format_array(a, indent, level),
            JsonValue::Object(o) => self.format_object(o, indent, level),
        }
    }

    pub fn generate(&mut self, value: &JsonValue) -> io::Result<()> {
        if let Some(indent) = &self.indent {
            self.format(value, indent, 0)
        } else {
            self.encode(value)
        }
    }
}

pub fn stringify(value: &JsonValue) -> JsonGenerateResult {
    let mut to = Vec::new();
    let mut gen = JsonGenerator::new(&mut to);
    gen.generate(value).map_err(|err| JsonGenerateError {
        msg: format!("{}", err),
    })?;
    Ok(String::from_utf8(to).unwrap())
}

pub fn format(value: &JsonValue) -> JsonGenerateResult {
    let mut to = Vec::new();
    let mut gen = JsonGenerator::new(&mut to).indent("  ");
    gen.generate(value).map_err(|err| JsonGenerateError {
        msg: format!("{}", err),
    })?;
    Ok(String::from_utf8(to).unwrap())
}

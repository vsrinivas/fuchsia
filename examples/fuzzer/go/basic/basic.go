package basic

func Fuzz(s []byte) {
	if len(s) == 4 && s[0] == 'F' && s[1] == 'U' && s[2] == 'Z' && s[3] == 'Z' {
		panic("fuzzed")
	}
}

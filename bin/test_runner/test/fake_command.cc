#include <cstring>
#include <iostream>

// Command that fails if given a single argument "fail".
int main(int argc, char** argv) {
  std::cout << "abc\n";
  std::cerr << "xyz\n";
  return (argc > 1 && std::strcmp(argv[1], "fail") == 0);
}

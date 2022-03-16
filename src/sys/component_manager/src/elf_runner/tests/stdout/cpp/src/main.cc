#include <chrono>
#include <iostream>
#include <thread>

int main() {
  std::cout << "Hello Stdout!" << std::endl;
  std::cerr << "Hello Stderr!" << std::endl;

  // TODO(https://fxbug.dev/95602) delete this sleep when clean shutdown works
  std::this_thread::sleep_until(std::chrono::system_clock::now() +
                                std::chrono::hours(std::numeric_limits<int>::max()));
}

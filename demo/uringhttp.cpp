
#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "async/Uring.hpp"
#include <print>

using namespace ACPAcoro;

auto &threadPoolInst = threadPool::getInstance();
uringInstance uringInst{threadPoolInst};

int main(int argc, char **argv) {
  if (argc < 3) {
    std::println("Usage: {} [port] [webRoot directory]", argv[0]);
    return 0;
  }
}

#pragma once

#define DEBUG(...) \
  if (true) {      \
  #ifdef
std::println(__VA_ARGS__);
}
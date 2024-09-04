#pragma once

#include <print>

#ifdef DEBUG
#define debug(...)                                                             \
  {                                                                            \
    std::print(                                                                \
        "\033[34mFunc: \033[33m{:<12} \033[34mLine: \033[33m{:<8}: \033[37m",  \
        __func__, __LINE__);                                                   \
    std::println(__VA_ARGS__);                                                 \
  }
#else

#define debug(...)                                                             \
  {}

#endif

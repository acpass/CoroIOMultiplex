#include "http/Socket.hpp"

#include <chrono>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>

tbb::concurrent_hash_map<std::weak_ptr<ACPAcoro::reactorSocket>,
                         std::chrono::system_clock::time_point>
    ACPAcoro::reactorSocket::timeoutTable{};
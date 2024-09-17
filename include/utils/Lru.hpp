#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
namespace ACPAcoro {

// A wrapper for the type T
template <typename T> using wrappedType = std::shared_ptr<T>;

// forward declaration
template <typename Key, typename Value> struct cacheFactory;

// A concept to check if a type is a capable value builder
template <typename Key, typename ValueBuilder>
concept buildableWithKey =
    requires(Key key, ValueBuilder builder) {
      // ValueBuilder::wrappedType;
      builder.build(key);
    } &&
    std::is_same_v<decltype(std::declval<ValueBuilder>().build(
                       std::declval<Key>())),
                   typename ValueBuilder::wrappedType>;

// virtual base class for the cache
// define the interface of the cache
// Key: the key type of the cache
// ValueBuilder: a type that can build a value with the key, defined by concept
// buildableWithKey
template <typename Key, typename ValueBuilder>
  requires buildableWithKey<Key, ValueBuilder>
struct cacheBase {

  enum class policy { LRU, LFU, FIFO };
  // usually a shared_ptr
  using valueType = ValueBuilder::wrappedType;

  // get a value from the cache, if the key is not in the cache, create a new
  // value and change the cache with specific strategy
  virtual valueType get(Key const &) = 0;
  // construct a new value with the key and put it into the cache
  virtual bool put(Key const &) = 0;
  virtual void refresh() = 0;
  virtual ~cacheBase() = default;

  constexpr int getCapacity() const { return capacity; }

  friend struct cacheFactory<Key, ValueBuilder>;

  cacheBase(int capacity) : capacity(capacity) {}

protected:
  size_t capacity;
  ValueBuilder builder;
};

// A LRU cache implementation
template <typename Key, typename ValueBuilder>
struct lruCache : public cacheBase<Key, ValueBuilder> {
  using valueType = typename cacheBase<Key, ValueBuilder>::valueType;
  using cacheListType = std::list<valueType>;
  using iteratorType = typename cacheListType::iterator;
  using iteratorMapType = std::unordered_map<Key, iteratorType>;

  // Get the value of the key if the key exists in the cache,
  // thread safe
  valueType get(Key const &key) override {
    // thread safe
    std::unique_lock<std::mutex> lock(mtx);

    // if the key is not in the cache, put it into the cache
    if (!map.contains(key)) {
      // if the put operation fails, return nullptr
      if (!put(key)) {
        return nullptr;
      }
      // otherwise the key is in the head of the list
      return *map[key];
    }

    // if the key is in the cache, move it to the head of the list
    if (map[key] != cacheList.begin()) {
      cacheList.splice(cacheList.begin(), cacheList, map[key]);
    }

    // map[key] is the iterator pointing to the value
    // return the value
    // usually a shared_ptr
    return *map[key];
  }

  // not thread safe
  bool put(Key const &key) override {
    if (map.contains(key)) {
      return false;
    }

    auto newNode = this->builder.build(key);
    if (newNode == nullptr) {
      return false;
    }

    if (map.size() == this->capacity) {
      auto endNode = cacheList.back();
      auto path = endNode->path();
      map.erase(path);
      cacheList.pop_back();
    }

    cacheList.push_front(newNode);
    map[key] = cacheList.begin();
    return true;
  }

  void refresh() override {
    std::unique_lock<std::mutex> lock(mtx);
    cacheList.clear();
    map.clear();
  }

  friend struct cacheFactory<Key, ValueBuilder>;

  lruCache(int capacity) : cacheBase<Key, ValueBuilder>(capacity) {}

private:
  std::mutex mtx;
  cacheListType cacheList;
  iteratorMapType map;
};

// A factory to create caches with different policies
template <typename Key, typename ValueBuilder> struct cacheFactory {
  using policy = typename cacheBase<Key, ValueBuilder>::policy;

  static std::unique_ptr<cacheBase<Key, ValueBuilder>> create(int capacity,
                                                              policy p) {
    switch (p) {
    case policy::LRU:
      return std::make_unique<lruCache<Key, ValueBuilder>>(capacity);
    case policy::LFU:
    case policy::FIFO:
      throw std::runtime_error("Not implemented yet");
    }
  }
};

} // namespace ACPAcoro

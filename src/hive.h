#pragma once

#include "nostd.h"
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

enum class hive_index_t : std::uint32_t {};

struct hive_entry_info_t {
  std::uint16_t chunk;
  std::uint16_t chunk_index;

  hive_index_t to_hive_index() { return std::bit_cast<hive_index_t>(*this); }
  static hive_entry_info_t from_hive_index(hive_index_t idx) {
    return std::bit_cast<hive_entry_info_t>(idx);
  }
};

struct chunk {
  union Data {
    std::byte data[];
    std::optional<uint16_t> next_free;
  };

  std::size_t stride;
  std::size_t size = 0;
  std::size_t capacity = 0;
  std::unique_ptr<std::byte[]> data;

  // TODO: fixed chunk allocation
  chunk(std::size_t stride)
      : stride(std::max(stride, sizeof(Data))), capacity(1024),
        data(new std::byte[stride * capacity]) {}

  auto get(uint16_t index) -> std::byte * {
    return data.get() + index * stride;
  }
  auto create(uint16_t index)
      -> std::pair<std::optional<uint16_t>, std::byte *> {
    auto *d = get(index);

    auto next_index = INLINE_LAMBDA->std::optional<uint16_t> {
      if (index < size) { // if mem was init
        return *reinterpret_cast<std::optional<uint16_t> *>(d);
      }

      size++;
      if (size == capacity) {
        return std::nullopt;
      }

      assert(size <= capacity);
      return static_cast<uint16_t>(size);
    };

    return {
        next_index,
        d,
    };
  }

  void remove(uint16_t index, std::optional<uint16_t> next_free) {
    auto d = reinterpret_cast<std::optional<std::uint16_t> *>(get(index));
    *d = next_free;
  }

  std::byte *begin() { return data.get(); }
  std::byte *end() { return data.get() + size * stride; }
};

class hive;
class hive_iterator {
  using chunk_iter = std::vector<chunk>::iterator;

  chunk_iter chunk_cur{};
  chunk_iter chunk_end{};
  std::byte *item_cur = nullptr;
  std::byte *item_end = nullptr;

public:
  std::byte *operator*() { return item_cur; }
  inline hive_iterator &operator++() {
    item_cur += chunk_cur->stride;
    if (item_cur != item_end) {
      return *this;
    }

    chunk_cur++;
    next_chunk();
    return *this;
  }

  /* inline hive_iterator operator++(int) { */
  /*   auto out = *this; */
  /*   ++this; */
  /*   return out; */
  /* } */
  void next_chunk() {
    if (chunk_cur == chunk_end) {
      *this = end();
      return;
    }
    item_cur = chunk_cur->begin();
    item_end = chunk_cur->end();
  }

  hive_iterator begin() { return *this; }
  hive_iterator end() {
    hive_iterator it{};
    it.chunk_cur = chunk_end;
    it.chunk_end = chunk_end;
    it.item_cur = nullptr;
    it.item_end = nullptr;
    return it;
  }

  friend inline bool operator==(const hive_iterator &a,
                                const hive_iterator &b) {
    return a.item_cur == b.item_cur && a.chunk_cur == b.chunk_cur;
  }

  hive_iterator() = default;
  hive_iterator(hive *h);
};

class hive {
private:
  std::vector<chunk> inner;
  std::optional<hive_entry_info_t> next_free;
  struct {
    std::size_t size;
  } tinfo;

public:
  hive(std::size_t size) : tinfo{size} {}

  auto get(hive_index_t idx) -> std::span<std::byte> {
    const auto info = hive_entry_info_t::from_hive_index(idx);
    assert(info.chunk <= inner.size());
    return {inner[info.chunk].get(info.chunk_index), tinfo.size};
  }

  auto create() -> std::pair<hive_index_t, std::byte *> {

    hive_entry_info_t info = INLINE_LAMBDA->hive_entry_info_t {
      if (next_free) {
        return *next_free;
      } else {
        inner.emplace_back(tinfo.size);
        return {
            .chunk = static_cast<std::uint16_t>(inner.size() - 1),
            .chunk_index = 0,
        };
      }
    };

    auto [new_next_free, data] = inner[info.chunk].create(info.chunk_index);
    next_free = INLINE_LAMBDA->std::optional<hive_entry_info_t> {
      if (new_next_free) {
        return hive_entry_info_t{
            .chunk = info.chunk,
            .chunk_index = *new_next_free,
        };
      }

      return std::nullopt;
    };

    return {info.to_hive_index(), data};
  }

  void remove(hive_index_t idx) {
    const auto info = hive_entry_info_t::from_hive_index(idx);
    std::optional<uint16_t> next_free_chunk_index;
    if (next_free) {
      next_free_chunk_index = next_free->chunk_index;
    }
    inner[info.chunk_index].remove(info.chunk_index, next_free_chunk_index);
    next_free = info;
  }

  hive_iterator begin() { return hive_iterator{this}.end(); }
  hive_iterator end() { return hive_iterator{this}; }
  friend hive_iterator;
};

inline hive_iterator::hive_iterator(hive *h)
    : chunk_cur(h->inner.begin()), chunk_end(h->inner.end()), item_cur(nullptr),
      item_end(nullptr) {
  next_chunk();
}

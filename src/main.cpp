#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <format>
#include <vector>

namespace nostd {

template <class... Args>
void println(std::format_string<Args...> fmt, Args &&...args) {
  std::puts(std::format(fmt, std::forward<Args>(args)...).c_str());
}

struct vec2 {
  float x, y;
};

template <class... T> struct typelist {};

template <class T> struct tail {};
template <class T, class... Tn> struct tail<typelist<T, Tn...>> {
  using type = typelist<Tn...>;
};
template <class T> using tail_t = tail<T>::type;

template <class A, class T> struct contains {};
template <class A, class... Tn> struct contains<A, typelist<Tn...>> {
  static constexpr bool value = (std::is_same_v<A, Tn> || ...);
};
template <class A, class T>
inline constexpr bool contains_v = contains<A, T>::value;

static_assert(contains_v<bool, typelist<bool>>);
static_assert(contains_v<int, typelist<bool, int>>);
static_assert(!contains_v<char, typelist<bool>>);
static_assert(!contains_v<char, typelist<bool, int>>);

template <class A, class T> struct index_of {};
template <class A, class... Tn> struct index_of<A, typelist<A, Tn...>> {
  static constexpr size_t value = 0;
};
template <class A, class... Tn> struct index_of<A, typelist<Tn...>> {
  static constexpr size_t value =
      1 + index_of<A, tail_t<typelist<Tn...>>>::value;
};
template <class A, class T>
inline constexpr size_t index_of_v = index_of<A, T>::value;

template <class T> char *type_name() {
  return abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
}

template <class T, const std::size_t N> class stack_vector {
private:
  std::array<T, N> data_;
  std::size_t size_ = 0;

public:
  using value_type = T;
  using iterator = decltype(data_)::iterator;
  using const_iterator = decltype(data_)::const_iterator;

  constexpr std::size_t size() const { return size_; };
  constexpr auto data() const { return data_.data(); };
  constexpr auto begin() { return data_.begin(); };
  constexpr auto end() { return data_.begin() + size_; };
  constexpr auto cbegin() const { return data_.cbegin(); };
  constexpr auto cend() const { return data_.cbegin() + size_; };

  constexpr const auto &operator[](std::size_t index) const {
    return data_[index];
  };
  constexpr auto &operator[](std::size_t index) { return data_[index]; };
  constexpr void resize(std::size_t size) {
    assert(size <= N);
    size_ = size;
  }
  constexpr void push_back(T &&t) {
    assert(size_ < N);
    data_[size_] = std::move(t);
    size_ += 1;
  }
};

} // namespace nostd

namespace ecs {
template <class A, class Components>
concept component = nostd::contains_v<A, Components>;

struct type_info {
  std::size_t size;
  std::size_t alignement;
};

template <const std::size_t N> struct Archetype {
  std::bitset<N> types;
  type_info tinfo;
  std::size_t size = 0;
  std::size_t capacity = 1024;

  std::byte *data;

  Archetype(std::bitset<N> types, type_info tinfo)
      : types(types), tinfo(tinfo), data(new(std::align_val_t(tinfo.alignement))
                                             std::byte[capacity * tinfo.size]) {
  }

  void insert(std::span<const std::byte> src) {
    assert(src.size_bytes() == tinfo.size);
    auto dst = at(size);
    std::copy(src.begin(), src.end(), dst.begin());
    size++;
  }

  ~Archetype() { delete[] data; }

  std::span<std::byte> at(std::size_t index) {
    assert(index < capacity);
    return {data + index * tinfo.size, tinfo.size};
  }

  void remove(size_t idx) {
    assert(idx < size);
    swap(idx, size - 1);
    size -= 1;
  }
  void swap(size_t idx1, size_t idx2) {
    if (idx1 == idx2) {
      return;
    }
    std::swap_ranges(at(idx1).begin(), at(idx1).end(), at(idx2).begin());
  }
  auto begin() { return data; }
  auto end() { return data + size * tinfo.size; }
};

template <class World, class... Ts> struct query_iterator {
  using archetypes_vector_iterator =
      std::vector<typename World::Archetype>::iterator;

  typename World::type_set types;
  archetypes_vector_iterator archetypes_vector_cur;
  archetypes_vector_iterator archetypes_vector_end;

  std::byte *archetype_cur;
  std::byte *archetype_end;

  std::array<std::size_t, sizeof...(Ts)> offsets;
  std::size_t stride;

  query_iterator &operator++() {
    archetype_cur += stride;
    if (archetype_cur != archetype_end) {
      return *this;
    }

    // find next archetype
    ++archetypes_vector_cur;
    while (archetypes_vector_cur != archetypes_vector_end &&
           (archetypes_vector_cur->types & types) != types) {
      ++archetypes_vector_cur;
    }
    if (archetypes_vector_cur == archetypes_vector_end) {
      *this = end();
      return *this;
    }

    stride = archetypes_vector_cur->tinfo.size;
    offsets = {World::template offset_in<Ts>(archetypes_vector_cur->types)...};
    archetype_cur = archetypes_vector_cur->begin();
    archetype_end = archetypes_vector_cur->end();
    return *this;
  }

  query_iterator operator++(int) {
    query_iterator out = *this;
    ++this;
    return out;
  }

  std::tuple<Ts &...> operator*() {
    std::size_t i = 0;
    return {
        ([&]() -> Ts & {
          return *reinterpret_cast<Ts *>(archetype_cur + offsets[i++]);
        }())...,
    };
  }

  friend auto operator<=>(const query_iterator &a,
                          const query_iterator &b) = default;

  auto begin() { return *this; }
  auto end() {
    return query_iterator{
        types,
        archetypes_vector_end,
        archetypes_vector_end,
        nullptr,
        nullptr,
        {},
        0,
    };
  }
};

template <class T>
concept trivial = std::is_trivial_v<T>;

template <trivial... Cs> class basic_world {
public:
  static constexpr size_t N = sizeof...(Cs);
  using components_t = nostd::typelist<Cs...>;
  using type_set = std::bitset<N>;
  using Archetype = Archetype<N>;

  template <component<components_t>... Ts> auto query() {
    if (archetypes_.empty()) {
      return query_iterator<basic_world, Ts...>{
          as_type_set<Ts...>(),
          archetypes_.begin(),
          archetypes_.end(),
          nullptr,
          nullptr,
          {},
          0,
      };
    }
    return query_iterator<basic_world, Ts...>{
        as_type_set<Ts...>(),      archetypes_.begin(),
        archetypes_.end(),         archetypes_[0].begin(),
        archetypes_[0].end(),      {offset_in<Ts>(archetypes_[0].types)...},
        archetypes_[0].tinfo.size,
    };
  }

  template <component<components_t>... Ts> void insert(Ts &&...ts) {
    constexpr std::size_t N = sizeof(std::tuple<Ts...>);
    std::array<std::byte, N> data;
    constexpr auto types = as_type_set<Ts...>();

    (
        [&] {
          const auto &src =
              std::span(reinterpret_cast<std::byte *>(&ts), sizeof(Ts));
          std::copy(src.begin(), src.end(),
                    data.begin() + offset_in<Ts>(types));
        }(),
        ...);

    auto &archetype = find_or_insert_archetype(
        as_type_set<Ts...>(), {sizeof(data), alignof(decltype(data))});
    archetype.insert(data);
  }

  /* private: */
  std::vector<Archetype> archetypes_;

  Archetype &find_or_insert_archetype(type_set types, type_info tinfo) {
    for (auto &archetype : archetypes_) {
      if (archetype.types == types) {
        return archetype;
      }
    }

    archetypes_.emplace_back(types, tinfo);
    return archetypes_.back();
  }

  template <component<components_t>... Ts>
  static consteval type_set as_type_set() {
    std::array<size_t, sizeof...(Ts)> v{nostd::index_of_v<Ts, components_t>...};
    std::bitset<N> b;
    for (const auto index : v) {
      b |= 1 << index;
    }
    return b;
  }

  template <component<components_t> T>
  static constexpr std::size_t offset_in(type_set types) {
    std::size_t offset = 0;
    std::array sizes = {
        sizeof(Cs)...,
    };
    for (std::size_t i = 0; i < nostd::index_of_v<T, components_t>; i++) {
      if (types.test(i)) {
        offset += sizes[i];
      }
    }
    return offset;
  }
};

template <class T> struct basic_world_from_list {};
template <class... Tn> struct basic_world_from_list<nostd::typelist<Tn...>> {
  using type = basic_world<Tn...>;
};

template <class T>
using basic_world_from_list_t = basic_world_from_list<T>::type;
} // namespace ecs

namespace components {
struct pos {
  nostd::vec2 pos;
};

struct speed {
  nostd::vec2 pos;
  nostd::vec2 pos2;
};

struct another_thing {
  nostd::vec2 pos;
  nostd::vec2 pos2;
  nostd::vec2 pos3;
};

using components = nostd::typelist<pos, speed, another_thing>;
} // namespace components

using World = ecs::basic_world_from_list_t<components::components>;

int main() {
  World world;

  nostd::println("World is {}", nostd::type_name<World>());
  for (size_t i = 0; i < 1024; i++) {
    world.insert<components::speed, components::another_thing, components::pos>(
        {.pos = {.x = static_cast<float>(i), .y = 0}}, {}, {});
  }

  nostd::println("Offset of speed in archetype<components::speed, "
                 "components::another_thing, components::pos>: {}",
                 world.offset_in<components::speed>(world.archetypes_[0].types),
                 sizeof(components::speed));

  for (const auto &[speed, pos] :
       world.query<components::speed, components::pos>()) {
    nostd::println("speed: {}, {}", speed.pos.x, pos.pos.x);
  }
}

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <format>
#include <iterator>
#include <tuple>
#include <typeindex>
#include <utility>
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

template <class T> union MaybeUninit {
  std::array<std::byte, sizeof(T)> uninit;
  T data;
  MaybeUninit() : uninit() {}
};

template <class T, const std::size_t N> class stack_vector {
private:
  std::array<MaybeUninit<T>, N> data_;
  std::size_t size_ = 0;

public:
  using value_type = T;

  constexpr std::size_t size() const { return size_; };
  constexpr auto data() const { return reinterpret_cast<T *>(data_.data()); };
  constexpr auto begin() { return reinterpret_cast<T *>(data_.begin()); };
  constexpr auto end() { return reinterpret_cast<T *>(data_.begin() + size_); };

  constexpr const auto &operator[](std::size_t index) const {
    return data_[index].data;
  };
  constexpr auto &operator[](std::size_t index) { return data_[index]; };
  constexpr void resize(std::size_t size) {
    assert(size <= N);
    size_ = size;
  }
  constexpr void push_back(T &&t) {
    assert(size_ < N);
    data_[size_].data = std::move(t);
    size_ += 1;
  }
};

} // namespace nostd

namespace ecs {

enum class entity_t : uint64_t {};

struct entity_info {
  uint16_t generation;
  uint16_t archetype;
  uint32_t idx;

  entity_t into_entity_t() { return std::bit_cast<entity_t>(*this); }
  static entity_info from_entity_t(entity_t ent) {
    return std::bit_cast<entity_info>(ent);
  }
};

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

  std::size_t insert(std::span<const std::byte> src) {
    assert(src.size_bytes() == tinfo.size);
    auto dst = at(size);
    std::copy(src.begin(), src.end(), dst.begin());
    ++size;

    return size - 1;
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

template <class... Ts> struct entity_getter {
  static std::tuple<Ts &...>
  from_data_offsets(std::byte *data,
                    std::array<std::size_t, sizeof...(Ts)> offsets) {
    std::size_t i = 0;
    return {

        ([&]() -> Ts & {
          return (*reinterpret_cast<Ts *>(data + offsets[i++]));
        }())...,
    };
  }
};

template <class World, class... Ts> class query_iterator {
  using archetypes_vector_iterator =
      std::vector<typename World::archetype>::iterator;

  struct M {
    typename World::type_set types;
    archetypes_vector_iterator archetypes_vector_cur;
    archetypes_vector_iterator archetypes_vector_end;

    std::byte *archetype_cur;
    std::byte *archetype_end;

    std::array<std::size_t, sizeof...(Ts)> offsets;
    std::size_t stride;
    World *world;

    friend auto operator<=>(const M &a, const M &b) = default;
  } m;

  query_iterator(M m) : m(m) {}

public:
  query_iterator &operator++() {
    if (m.archetype_cur != nullptr) {
      m.archetype_cur += m.stride;
      if (m.archetype_cur != m.archetype_end) {
        return *this;
      }

      ++m.archetypes_vector_cur;
    }

    // Find next useful archetype
    while (m.archetypes_vector_cur != m.archetypes_vector_end &&
           (m.archetypes_vector_cur->types & m.types) != m.types) {
      ++m.archetypes_vector_cur;
    }
    if (m.archetypes_vector_cur == m.archetypes_vector_end) {
      *this = end();
      return *this;
    }

    m.stride = m.archetypes_vector_cur->tinfo.size;
    m.offsets = {
        m.world->template offset_in<Ts>(m.archetypes_vector_cur->types)...};
    m.archetype_cur = m.archetypes_vector_cur->begin();
    m.archetype_end = m.archetypes_vector_cur->end();
    return *this;
  }

  query_iterator operator++(int) {
    query_iterator out = *this;
    ++this;
    return out;
  }

  std::tuple<Ts &...> operator*() {
    return entity_getter<Ts...>::from_data_offsets(m.archetype_cur, m.offsets);
  }

  friend auto operator<=>(const query_iterator &a,
                          const query_iterator &b) = default;

  query_iterator begin() { return *this; }
  query_iterator end() {
    return M{
        m.types,
        m.archetypes_vector_end,
        m.archetypes_vector_end,
        nullptr,
        nullptr,
        {},
        0,
        m.world,
    };
  }

  query_iterator(World *world)
      : m{
            .types = world->template as_type_set<Ts...>(),
            .archetypes_vector_cur = world->archetypes_.begin(),
            .archetypes_vector_end = world->archetypes_.end(),
            .world = world,
        } {}
};

template <class T>
concept trivial = std::is_trivial_v<T>;

template <trivial... Cs> struct StaticRegistry {
  using components_t = nostd::typelist<Cs...>;
  static constexpr std::size_t max_components = sizeof...(Cs);
  static constexpr std::array sizes = {
      sizeof(Cs)...,
  };

  std::size_t size(std::size_t type_index) const { return sizes[type_index]; }

  template <component<components_t> T> std::size_t index() const {
    return nostd::index_of_v<T, components_t>;
  }
};

template <const std::size_t N> struct DynamicRegistry {
  static constexpr std::size_t max_components = N;
  struct RegistryEntry {
    std::type_index type_idx;
    size_t size;
  };
  nostd::stack_vector<RegistryEntry, N> entries{};

  std::size_t size(std::size_t idx) const { return entries[idx].size; }

  template <class T> std::size_t index() { return register_type<T>(); }

  template <class T> std::size_t register_type() {
    const std::type_index key(typeid(T));
    if (const auto it =
            std::ranges::find(entries, key, &RegistryEntry::type_idx);
        it != entries.end()) {
      return std::distance(entries.begin(), it);
    }

    assert(entries.size() < N);
    entries.push_back({key, sizeof(T)});
    return entries.size() - 1;
  }
};

template <class Registry> class basic_world {
public:
  using type_set = std::bitset<Registry::max_components>;
  using archetype = Archetype<Registry::max_components>;
  Registry registry;

  template <class... Ts> auto query() {
    return ++query_iterator<basic_world, Ts...>{
        this,
    };
  }

  template <class... Ts> auto entity(const entity_t ent) {
    const auto ent_info = entity_info::from_entity_t(ent);

    auto &archetype = archetypes_[ent_info.archetype];
    const auto needed = as_type_set<Ts...>();
    assert((needed & archetype.types) == needed);

    return entity_getter<Ts...>::from_data_offsets(
        archetype.at(ent_info.idx).data(),
        {
            offset_in<Ts>(archetype.types)...,
        });
  }

  template <class... Ts> entity_t insert(Ts &&...ts) {
    constexpr std::size_t N = sizeof(std::tuple<Ts...>);
    std::array<std::byte, N> data;
    auto types = as_type_set<Ts...>();

    (
        [&] {
          const auto &src =
              std::span(reinterpret_cast<std::byte *>(&ts), sizeof(Ts));
          std::copy(src.begin(), src.end(),
                    data.begin() + offset_in<Ts>(types));
        }(),
        ...);

    const std::size_t archetype_idx = find_or_insert_archetype_idx(
        as_type_set<Ts...>(), {sizeof(data), alignof(decltype(data))});

    auto &archetype = archetypes_[archetype_idx];
    const auto idx = archetype.insert(data);

    return entity_info{
        .generation = 0,
        .archetype = static_cast<uint16_t>(archetype_idx),
        .idx = static_cast<uint32_t>(idx),
    }
        .into_entity_t();
  }

  /* private: */
  std::vector<archetype> archetypes_;

  std::size_t find_or_insert_archetype_idx(type_set types, type_info tinfo) {
    for (std::size_t i = 0; i < archetypes_.size(); i++) {
      if (archetypes_[i].types == types) {
        return i;
      }
    }

    archetypes_.emplace_back(types, tinfo);
    return archetypes_.size() - 1;
  }

  template <class... Ts> constexpr type_set as_type_set() {
    std::array<size_t, sizeof...(Ts)> v{registry.template index<Ts>()...};
    std::bitset<Registry::max_components> b;
    for (const auto index : v) {
      b |= 1 << index;
    }
    return b;
  }

  template <class T> constexpr std::size_t offset_in(type_set types) {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < registry.template index<T>(); i++) {
      if (types.test(i)) {
        offset += registry.size(i);
      }
    }
    return offset;
  }
};

template <class T> struct static_registry_from_list {};
template <class... Tn>
struct static_registry_from_list<nostd::typelist<Tn...>> {
  using type = StaticRegistry<Tn...>;
};

template <class T>
using static_registry_from_list_t = static_registry_from_list<T>::type;
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

using World =
    ecs::basic_world<ecs::static_registry_from_list_t<components::components>>;
using DynamicWorld = ecs::basic_world<ecs::DynamicRegistry<8>>;

int main() {
  DynamicWorld world;

  nostd::println("World is {}", nostd::type_name<World>());

  ecs::entity_t ent254;
  for (size_t i = 0; i < 1024; i++) {
    const ecs::entity_t ent = world.insert<components::speed, components::pos>(
        {.pos = {.x = static_cast<float>(i), .y = 2}}, {.pos={2, 4}});

    if (i == 254) {
      ent254 = ent;
    }
  }

  for (const auto &[speed, pos] :
       world.query<components::speed, components::pos>()) {
    nostd::println("speed: {}, {}", speed.pos.x, pos.pos.x);
  }
  {
    const auto &[speed, pos] =
        world.entity<components::speed, components::pos>(ent254);
    nostd::println("ent254: speed: {}, {}", speed.pos.x, pos.pos.x);
  }
}

#pragma once

#include <array>
#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <vector>

#include "hive.h"
#include "nostd.h"

namespace ecs {

enum class entity_t : uint64_t {};

struct entity_info {
  uint16_t generation;
  uint16_t archetype;
  hive_index_t idx;

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

  hive data;

  Archetype(std::bitset<N> types, type_info tinfo)
      : types(types), tinfo(tinfo), data(tinfo.size) {}

  hive_index_t insert(std::span<const std::byte> src) {
    auto [idx, d] = data.create();
    std::copy(src.begin(), src.end(), d);

    return idx;
  }

  std::span<std::byte> at(hive_index_t index) { return data.get(index); }

  auto begin() { return data.end(); }
  auto end() { return data.begin(); }
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
template <class... Ts> struct entity_ptr_getter {
  static std::tuple<Ts *...>
  from_data_offsets(std::byte *data,
                    std::array<std::size_t, sizeof...(Ts)> offsets) {
    std::size_t i = 0;
    return {

        ([&]() -> Ts * {
          return (reinterpret_cast<Ts *>(data + offsets[i++]));
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

    hive_iterator archetype_cur;
    hive_iterator archetype_end;

    std::array<std::size_t, sizeof...(Ts)> offsets;
    World *world;

    friend inline auto operator==(const M &a, const M &b) {
      return a.archetype_cur == b.archetype_cur;
    }
  } m;

  query_iterator(M m) : m(m) {}

public:
  void find_next_archetype() {
    while (m.archetypes_vector_cur != m.archetypes_vector_end &&
           (m.archetypes_vector_cur->types & m.types) != m.types) {
      ++m.archetypes_vector_cur;
    }
    if (m.archetypes_vector_cur == m.archetypes_vector_end) {
      *this = end();
      return;
    }

    m.offsets = {
        m.world->template offset_in<Ts>(m.archetypes_vector_cur->types)...};
    m.archetype_cur = m.archetypes_vector_cur->begin();
    m.archetype_end = m.archetypes_vector_cur->end();
  }

  query_iterator &operator++() {
    ++m.archetype_cur;
    if (m.archetype_cur != m.archetype_end) {
      return *this;
    }

    ++m.archetypes_vector_cur;
    find_next_archetype();
    return *this;
  }

  inline query_iterator operator++(int) {
    query_iterator out = *this;
    ++this;
    return out;
  }

  inline std::tuple<Ts &...> operator*() { return next(); }
  inline std::tuple<Ts &...> next() {
    return entity_getter<Ts...>::from_data_offsets(*m.archetype_cur, m.offsets);
  }
  inline std::tuple<Ts *...> next_ptr() {
    return entity_ptr_getter<Ts...>::from_data_offsets(*m.archetype_cur,
                                                       m.offsets);
  }

  friend auto operator<=>(const query_iterator &a,
                          const query_iterator &b) = default;

  query_iterator begin() { return *this; }
  query_iterator end() {
    return M{
        m.types, m.archetypes_vector_end, m.archetypes_vector_end, {}, {}, {},
        m.world,
    };
  }

  query_iterator(World *world)
      : m{
            .types = world->template as_type_set<Ts...>(),
            .archetypes_vector_cur = world->archetypes_.begin(),
            .archetypes_vector_end = world->archetypes_.end(),
            .world = world,
        } {
    find_next_archetype();
  }
};

template <nostd::trivial... Cs> struct StaticRegistry {
  using components_t = nostd::typelist<Cs...>;
  static constexpr std::size_t max_components = sizeof...(Cs);
  static constexpr std::array sizes = {
      sizeof(Cs)...,
  };

  constexpr std::size_t size(std::size_t type_index) const {
    return sizes[type_index];
  }

  template <component<components_t> T> constexpr std::size_t index() {
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
    return query_iterator<basic_world, Ts...>{
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
    auto types = as_type_set<std::remove_cvref_t<Ts>...>();

    (
        [&] {
          const auto &src =
              std::span(reinterpret_cast<const std::byte *>(&ts), sizeof(Ts));
          std::copy(src.begin(), src.end(),
                    data.begin() + offset_in<std::remove_cvref_t<Ts>>(types));
        }(),
        ...);

    const std::size_t archetype_idx = find_or_insert_archetype_idx(
        types, {sizeof(data), alignof(decltype(data))});

    auto &archetype = archetypes_[archetype_idx];
    const auto idx = archetype.insert(data);

    return entity_info{
        .generation = 0,
        .archetype = static_cast<uint16_t>(archetype_idx),
        .idx = idx,
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
    const auto t_index = registry.template index<T>();
    for (std::size_t i = 0; i < t_index; i++) {
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
using DynamicWorld = ecs::basic_world<ecs::DynamicRegistry<8>>;
} // namespace ecs

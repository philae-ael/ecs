#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <format>
#include <functional>
#include <utility>

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

template <class T>
concept trivial = std::is_trivial_v<T>;

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

  constexpr void clear() { size_ = 0; }
};

struct inline_lambda {
  template <class Fn> auto operator+(Fn &&f) { return f(); }
};

struct timed_inline_lambda {
  std::string_view name;

  template <class Fn> auto operator+(Fn &&f) {
    const auto start = std::chrono::high_resolution_clock::now();
    f();
    const auto end = std::chrono::high_resolution_clock::now();
    float duration_s =
        std::chrono::duration_cast<std::chrono::duration<float>>(end - start)
            .count();
    nostd::println("{}: {:.0f}us", name, duration_s * 1000 * 1000);
  }
};

template <class Fn, class T, T... ints>
void static_for(std::integer_sequence<T, ints...>, Fn &&f) {
  (std::invoke(f, ints), ...);
};
} // namespace nostd
#define INLINE_LAMBDA nostd::inline_lambda{} + [&]()

#ifdef TIMED_INLINE_LAMBDA_DISABLE
#define TIMED_INLINE_LAMBDA(name) nostd::inline_lambda{} + [&]()
#else
#define TIMED_INLINE_LAMBDA(name) nostd::timed_inline_lambda{(name)} + [&]()
#endif

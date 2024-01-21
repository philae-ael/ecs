#include "ecs.h"
#include "nostd.h"
#include <SDL_events.h>
#include <SDL_rect.h>
#include <SDL_render.h>
#include <SDL_video.h>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>

#include <SDL2/SDL.h>
#include <numbers>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace components {
struct pos : nostd::vec2 {};
struct speed : nostd::vec2 {};
struct particule_info {
  float mass;
  float lifetime;
};

using list = nostd::typelist<pos, speed, particule_info>;
} // namespace components

class Renderer {
  struct M {
    SDL_Window *win;
    SDL_Renderer *ren;
  } m;
  Renderer(M &&m) : m(std::move(m)) {}

public:
  static std::optional<Renderer> init() {
    Renderer renderer{M{}};

    renderer.m.win =
        SDL_CreateWindow("Hello World!", 100, 100, 620, 387, SDL_WINDOW_SHOWN);
    if (renderer.m.win == nullptr) {
      std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
      return std::nullopt;
    }

    renderer.m.ren = SDL_CreateRenderer(renderer.m.win, -1,
                                        SDL_RENDERER_ACCELERATED |
                                            SDL_RENDERER_PRESENTVSYNC);
    if (renderer.m.ren == nullptr) {
      std::cerr << "SDL_CreateRenderer Error" << SDL_GetError() << std::endl;
      SDL_Quit();
      return std::nullopt;
    }

    SDL_GL_SetSwapInterval(0);

    return std::move(renderer);
  }

  template <class Registry> void draw(ecs::basic_world<Registry> &world) {
    int width;
    int height;
    SDL_GetRendererOutputSize(m.ren, &width, &height);
    float half_width = width / 2.0;
    float half_height = height / 2.0;

    auto query = world.template query<components::pos>();
    std::vector<SDL_FPoint> points;
    for (auto [pos] : query) {
      SDL_FPoint point{
          .x = (half_width + pos.x),
          .y = (half_height + pos.y),
      };
      points.push_back(point);
    }

    SDL_SetRenderDrawColor(m.ren, 0x00, 0x00, 0x00, 0xff);
    SDL_RenderClear(m.ren);
    SDL_SetRenderDrawColor(m.ren, 0xff, 0xff, 0xff, 0xff);
    SDL_RenderDrawPointsF(m.ren, points.data(), points.size());
    SDL_RenderPresent(m.ren);
  }

  ~Renderer() {
    if (m.ren != nullptr) {
      SDL_DestroyRenderer(m.ren);
    }
    if (m.win != nullptr) {
      SDL_DestroyWindow(m.win);
    }
  }

  Renderer(Renderer &&r) noexcept : m(std::exchange(r.m, M{})) {}
  Renderer(Renderer &r) = delete;
};

struct Particule {
  components::speed speed;
  components::pos pos;

  components::particule_info particle_info;
};

struct ParticleManager {
  std::random_device rd;
  std::mt19937 gen;
  std::uniform_real_distribution<> dis_angle;
  std::uniform_real_distribution<> dis_amplitude;
  std::uniform_real_distribution<> dis_mass;
  std::uniform_real_distribution<> dis_lifetime;

  ParticleManager()
      : gen(rd()), dis_angle(0.0, 2 * std::numbers::pi),
        dis_amplitude(5, 100.0), dis_mass(1, 10.0), dis_lifetime(0.0, 5.0) {}

  template <class Registry>
  void create_particles(ecs::basic_world<Registry> &world, std::size_t amount) {
    for (std::size_t i = 0; i < amount; i++) {
      const auto [speed, pos, lifetime] = create_particle();

      world.insert(pos, speed, lifetime);
    }
  }

  template <class Registry>
  void update(ecs::basic_world<Registry> &world, float dt) {
    auto query = world.template query<components::pos, components::speed,
                                      components::particule_info>();
    for (auto [pos, speed, particule_info] : query) {
      particule_info.lifetime -= dt;
      if (particule_info.lifetime <= 0) {
        const auto [new_speed, new_pos, new_lifetime] = create_particle();
        pos = new_pos;
        speed = new_speed;
        particule_info = new_lifetime;
      }
    }
  }

private:
  Particule create_particle() {
    float angle = dis_angle(gen);
    float amplitude = dis_amplitude(gen);
    float mass = dis_mass(gen);
    float lifetime = dis_lifetime(gen);

    return {
        .speed =
            {
                std::sin(angle) * amplitude,
                std::cos(angle) * amplitude,
            },
        .pos = {0.0, 0.0},
        .particle_info =
            {
                mass,
                lifetime,
            },
    };
  }
};

using fsec = std::chrono::duration<float>;

template <class Registry>
void update_physics(ecs::basic_world<Registry> &world, float dt) {
  auto query = world.template query<components::pos, components::speed,
                                    components::particule_info>();
  for (auto [pos, speed, particule_info] : query) {
    pos.x += speed.x * dt;
    pos.y += speed.y * dt;
    speed.y += 100.0f * dt / particule_info.mass;
  }
}

int main() {
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
    std::abort();
  }

  /* ecs::basic_world<ecs::static_registry_from_list_t<components::list>> world{}; */
  ecs::DynamicWorld world{};
  Renderer renderer = INLINE_LAMBDA {
    auto renderer = Renderer::init();
    if (!renderer) {
      std::abort();
    }
    return std::move(renderer.value());
  };

  ParticleManager particles{};
  particles.create_particles(world, 4 * 4 * 1024);

  float dt = 0.0;
  auto last = std::chrono::high_resolution_clock::now();
  bool quit = false;
  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        quit = true;
        break;
      default:
        break;
      }
    }

    TIMED_INLINE_LAMBDA("update physics") { update_physics(world, dt); };
    TIMED_INLINE_LAMBDA("update particles") { particles.update(world, dt); };
    TIMED_INLINE_LAMBDA("render") { renderer.draw(world); };

    const auto previous =
        std::exchange(last, std::chrono::high_resolution_clock::now());
    dt = std::chrono::duration_cast<fsec>(last - previous).count();
  }

  SDL_Quit();
}

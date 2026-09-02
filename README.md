# ByteEngine

A header-only **Entity Component System (ECS)** for **C++23**. Archetype storage, structure-of-arrays (SoA) columns, component queries, and bulk structural operations.

## Features

- Header-only
- C++23
- Archetype storage
- Structure-of-arrays (SoA)
- `view<T...>()` and `each<T...>()` queries
- Bulk create, attach, detach, and destroy
- Configurable debug checks (`BYTE_ECS_CHECKS`)

## Requirements

- C++23 compiler (Clang 16+, GCC 13+, or MSVC 19.3x+ with `/std:c++latest`)
- `<flat_map>` support where `Archetype` is used

## Project layout

```
src/
├── ecs.hpp           ← include this
├── pool.hpp
├── archetype.hpp
├── bulk.hpp
├── poolview.hpp
├── set_map.hpp
├── sparse_vector.hpp
└── ...
```

This repo ships **`src/`** and this README. No build system or benchmarks are included.

## Installation

Clone and add `src` to your include path:

```bash
git clone https://github.com/guardeat/BECS.git
cd BECS
```

```bash
clang++ -std=c++23 -Wall -Wextra -Isrc your_app.cpp
```

Release build (checks off via `NDEBUG`):

```bash
clang++ -std=c++23 -O3 -DNDEBUG -Isrc your_app.cpp
```

`-Isrc` lets you write `#include "ecs.hpp"` instead of `#include "src/ecs.hpp"`.

## Quick example

```cpp
#include "ecs.hpp"

struct Position {
  float x = 0;
  float y = 0;
};

struct Velocity {
  float x = 0;
  float y = 0;
};

int main() {
  byte::Pool<> world;

  byte::EntityID entity = world.create<Position>();
  world.attach<Velocity>(entity, Velocity{1.f, 0.f});

  world.each<Position, Velocity>([](Position& pos, Velocity& vel) {
    pos.x += vel.x;
    pos.y += vel.y;
  });

  for (auto [pos, vel] : world.view<Position, Velocity>()) {
    (void)pos;
    (void)vel;
  }

  auto entities = world.bulk().create<Position>(1024);
  world.bulk().attach(entities, Velocity{});

  world.destroy(entity);
}
```

## API

### Create entities

```cpp
byte::EntityID entity = world.create();
byte::EntityID entity = world.create<Position>();
byte::EntityID entity = world.create<Position, Velocity>();
```

### Access components

```cpp
world.contains(entity);
world.has<Position>(entity);
Position& position = world.get<Position>(entity);
```

### Structural changes

```cpp
world.attach<Velocity>(entity);
world.attach<Velocity>(entity, Velocity{1.f, 0.f});
world.detach<Velocity>(entity);
world.destroy(entity);
```

### Queries

`each` is usually faster; `view` supports range-for:

```cpp
world.each<Position, Velocity>([](Position& pos, Velocity& vel) {
  pos.x += vel.x;
});

for (auto [pos, vel] : world.view<Position, Velocity>()) {
  pos.x += vel.x;
}
```

`view<Position, Velocity>()` matches every archetype that **contains both** types, e.g.:

- `Position + Velocity`
- `Position + Velocity + Health`
- `Position + Velocity + Health + Transform`

### Bulk operations

```cpp
auto entities = world.bulk().create<Position>(10'000);
world.bulk().attach(entities, Velocity{});
world.bulk().detach<Velocity>(entities);
world.bulk().destroy(entities);
```

Structural changes are grouped by source archetype when possible.

## How it works

Entities with the same component set live in one **archetype**. Each component type is a contiguous column (SoA):

```
Position:  [ P ][ P ][ P ][ P ]
Velocity:  [ V ][ V ][ V ][ V ]
Health:    [ H ][ H ][ H ][ H ]
```

Adding or removing a component **migrates** the row to another archetype:

```
Position  --attach<Velocity>()-->  Position + Velocity
```

### Entity IDs

`byte::EntityID` is a handle. The pool stores a **location** per entity:

```
EntityID → Location → { Archetype*, row }
```

## Public aliases

Include `ecs.hpp` and use the `byte` namespace:

| Alias | Maps to |
|-------|---------|
| `byte::EntityID` | `byte::ecs::EntityID` |
| `byte::Pool<>` | `byte::ecs::Pool<>` |
| `byte::Archetype<>` | `byte::ecs::Archetype<>` |
| `byte::Bulk<Pool>` | `byte::ecs::Bulk<Pool>` |
| `byte::PoolView<...>` | `byte::ecs::PoolView<...>` |
| `byte::ArchetypeView<...>` | `byte::ecs::ArchetypeView<...>` |
| `byte::ComponentInfo` | `byte::ecs::ComponentInfo` |
| `byte::TypeID`, `byte::TypeIdV<T>` | compile-time type identity |

PascalCase types under `byte::ecs` remain available if you prefer them.

## Runtime checks

- **Debug** (no `NDEBUG`): checks **on** by default
- **Release** (`-DNDEBUG`): checks **off** by default

Override manually:

```bash
clang++ -std=c++23 -DBYTE_ECS_CHECKS=1 -Isrc your_app.cpp
clang++ -std=c++23 -DBYTE_ECS_CHECKS=0 -Isrc your_app.cpp
```

## Design

1. **Archetypes** — one storage layout per component signature
2. **SoA columns** — cache-friendly contiguous component arrays
3. **Signature lookup** — `set_map` finds archetypes by superset query for `view` / `each`

## Roadmap

- [ ] More tests
- [ ] Benchmarks
- [ ] CI (Clang, GCC, MSVC)
- [ ] More query features
- [ ] More examples

## Contributing

Issues and pull requests are welcome. Please keep changes:

- C++23-compatible
- Header-only
- Focused and consistent with the existing API

## License

MIT — see [LICENSE](LICENSE) in the repository.

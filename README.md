# ByteECS

A fast, header-only **Entity Component System (ECS)** written in **C++23**. Built with archetype table storage, structure-of-arrays (SoA) contiguous columns, superset-indexed queries via inverted maps, bulk operations, and deferred mutation command buffers for safe in-query modifications.

---

## Features

- **Header-Only & Dependency-Free**: Pure modern standard C++23.
- **Archetype Storage**: Entities sharing the same component signature reside in dedicated archetype tables.
- **Structure of Arrays (SoA)**: Components are stored in contiguous typed memory columns for optimal cache locality and SIMD friendliness.
- **Fast Queries**:
  - `world.each<T...>(fn)` for maximum iteration performance.
  - `world.view<T...>()` for standard C++ range-for loops.
  - Query filtering via `.with<T...>()` and `.without<T...>()`.
  - Direct entity handle access: include `byte::EntityID` in query signatures.
- **Deferred Mutations (`defer` / `flush`)**: Safely queue component attachments, detachments, and entity destructions during queries without invalidating iterators.
- **Bulk Operations (`bulk`)**: Amortize archetype migrations by creating, attaching, detaching, or destroying entities in batches.
- **Custom Allocators**: Parameterized allocator support throughout `Pool<Alloc>` and `Archetype<Alloc>`.
- **Configurable Runtime Checks**: Zero-cost assertions in release builds with customizable override flags (`BYTE_ECS_CHECKS`).

---

## Requirements

- **C++23** conforming compiler:
  - Clang 16+
  - GCC 13+
  - MSVC 19.3x+ (`/std:c++latest`)
- Standard library support for `<flat_map>`.

---

## Project Layout

```text
.
├── src/
│   ├── ecs.hpp             # Umbrella header (includes everything, exposes byte:: aliases)
│   ├── pool.hpp            # Primary ECS coordinator (byte::ecs::Pool)
│   ├── archetype.hpp       # Table storage for a single component signature (SoA)
│   ├── archeview.hpp       # Direct iterator view over a single archetype
│   ├── poolview.hpp        # Multi-archetype query view with .with() and .without()
│   ├── bulk.hpp            # Batch creation and migration utilities
│   ├── defer.hpp           # Deferred command buffer for safe mutations
│   ├── column.hpp          # Contiguous type-erased component column
│   ├── component_info.hpp  # Runtime metadata & lifecycle hooks for components
│   ├── entity.hpp          # EntityID handle definition
│   ├── inverted_map.hpp    # Fast superset archetype index for queries
│   ├── sparse_vector.hpp   # Sparse set backing entity lookups and recycling
│   ├── type_id.hpp         # Compile-time type identity hashing
│   ├── type_name.hpp       # Compile-time type name reflection
│   ├── fnv1a.hpp           # FNV-1a 64-bit compile-time hash implementation
│   └── ecs_assert.hpp      # Configurable assertions and check macros
├── main.cpp                # Comprehensive performance benchmarks and examples
├── compile_flags.txt       # Clang / clangd compilation flags
├── LICENSE                 # MIT License
└── README.md
```

---

## Installation & Usage

ByteECS is header-only. Simply clone the repository and add `src/` to your compiler's include path:

```bash
git clone https://github.com/guardeat/BECS.git
cd BECS
```

### Compile Flags

Add `-Isrc` (or `--include-directory=src`) and enable C++23:

```bash
# Debug build (checks enabled by default)
clang++ -std=c++23 -Wall -Wextra -Isrc your_app.cpp -o your_app

# Release build (checks disabled by default via NDEBUG)
clang++ -std=c++23 -O3 -DNDEBUG -Isrc your_app.cpp -o your_app
```

### Running the Included Benchmarks

To compile and execute the benchmark suite in `main.cpp`:

```bash
clang++ -std=c++23 -O3 -DNDEBUG -Wall -Wextra -Isrc main.cpp -o bench
./bench
```

---

## Quick Example

```cpp
#include "ecs.hpp"
#include <iostream>

struct Position { float x = 0.f; float y = 0.f; };
struct Velocity { float x = 1.f; float y = 2.f; };
struct Health   { int hp = 100; };
struct Frozen   {};

int main() {
  byte::Pool world;

  // Create entities with initial components
  byte::EntityID player = world.create<Position, Velocity, Health>();
  byte::EntityID enemy  = world.create<Position, Velocity>();

  // Attach / detach components
  world.attach<Frozen>(enemy);
  world.detach<Frozen>(enemy);

  // 1. High-speed iteration with each()
  world.each<Position, Velocity>([](Position& pos, const Velocity& vel) {
    pos.x += vel.x;
    pos.y += vel.y;
  });

  // 2. Query with EntityID and filter modifiers (.without / .with)
  for (auto [id, pos, vel] : world.view<byte::EntityID, Position, Velocity>().without<Frozen>()) {
    std::cout << "Entity #" << id << " at (" << pos.x << ", " << pos.y << ")\n";
  }

  // 3. Deferred mutations during iteration (safe from iterator invalidation)
  world.each<byte::EntityID, Health>([&](byte::EntityID id, Health& hp) {
    if (hp.hp <= 0) {
      world.defer().destroy(id);
    } else {
      world.defer().attach(id, Frozen{});
    }
  });
  world.flush(); // Applies queued detaches -> attaches -> destroys

  // 4. High-performance bulk operations
  auto minions = world.bulk().create<Position>(10'000);
  world.bulk().attach(minions, Velocity{0.5f, 0.5f});
  world.bulk().destroy(minions);

  world.destroy(player);
}
```

---

## API Reference

### 1. Entity Lifecycle

```cpp
// Create an empty entity
byte::EntityID e1 = world.create();

// Create an entity with initial components
byte::EntityID e2 = world.create<Position>();
byte::EntityID e3 = world.create<Position, Velocity>(Position{1.f, 2.f}, Velocity{0.f, 1.f});

// Check existence
bool alive = world.contains(e1);

// Destroy an entity
world.destroy(e1);

// Total alive entities
std::size_t count = world.size();

// Clear all entities across all archetypes
world.clear();
```

### 2. Component Management

```cpp
// Check if entity has a specific component
bool has_vel = world.has<Velocity>(entity);

// Retrieve component reference
Position& pos = world.get<Position>(entity);
const Position& const_pos = std::as_const(world).get<Position>(entity);

// Attach a default-constructed component
world.attach<Velocity>(entity);

// Attach with a value (forwards to emplace)
world.attach<Velocity>(entity, Velocity{2.f, 3.f});

// Emplace directly in-place with constructor arguments (supports non-default-constructible types)
Transform& t = world.emplace<Transform>(entity, 10.f, 20.f, 0.f);

// Detach component
world.detach<Velocity>(entity);
```

### 3. Queries and Iteration

#### `world.each<T...>(callback)`
Executes a callback directly over contiguous column spans. Offers optimal compiler vectorization and eliminates per-entity tuple construction overhead.

```cpp
world.each<Position, Velocity>([](Position& pos, Velocity& vel) {
  pos.x += vel.x;
  pos.y += vel.y;
});
```

#### `world.view<T...>()`
Returns an iterable view compatible with range-based `for` loops and structured binding syntax:

```cpp
for (auto [pos, vel] : world.view<Position, Velocity>()) {
  pos.x += vel.x;
}
```

#### Accessing `EntityID`
Include `byte::EntityID` anywhere in the query type list:

```cpp
world.each<byte::EntityID, Position>([](byte::EntityID id, Position& pos) {
  // Use id and pos
});

for (auto [id, pos] : world.view<byte::EntityID, Position>()) {
  // Use id and pos
}
```

#### Query Filtering (`.with` / `.without`)
Refine archetype matches without including those components in the iteration parameters:

```cpp
// Match entities having Position and Velocity, but NOT Dead, and MUST have Shield
auto query = world.view<Position, Velocity>()
                  .without<Dead>()
                  .with<Shield>();

for (auto [pos, vel] : query) {
  pos.x += vel.x;
}
```

---

### 4. Deferred Operations (`defer` / `flush`)

Modifying entity archetypes (attaching, detaching, or destroying) during iteration relocates rows and invalidates iterators. `world.defer()` records structural commands into a memory buffer and applies them in a safe, batch-optimized order when calling `world.flush()`.

```cpp
// Queue operations during iteration
world.each<byte::EntityID, Health>([&](byte::EntityID id, Health& h) {
  if (h.hp <= 0) {
    world.defer().destroy(id);
  } else if (h.hp < 20) {
    world.defer().emplace<PoisonEffect>(id, /*damage=*/5, /*duration=*/10.f);
  }
});

// Queue detached components
world.defer().detach<Shield>(player_id);

// Check deferred queue state
bool has_pending = !world.defer().empty();
std::size_t pending_count = world.defer().size();

// Execute all queued operations
// Execution order: detaches -> attaches -> destroys
world.flush();

// Or discard queued commands without applying
world.defer().clear();
```

---

### 5. Bulk Operations (`bulk`)

When manipulating large numbers of entities, individual migrations incur per-entity archetype lookups and book-keeping. `world.bulk()` groups entities by their source archetype and executes migrations in contiguous chunks:

```cpp
// Create 50,000 entities in one allocation
std::vector<byte::EntityID> army = world.bulk().create<Position>(50'000);

// Attach a component in bulk
world.bulk().attach(army, Velocity{1.f, 0.f});

// Emplace a component in bulk with constructor arguments
world.bulk().emplace<Config>(army, 128, "agent");

// Emplace in bulk with a per-entity generator function
world.bulk().emplace_with<PlayerId>(army, [](byte::EntityID id) {
  return PlayerId{id};
});

// Detach a component in bulk
world.bulk().detach<Velocity>(army);

// Destroy entities in bulk
world.bulk().destroy(army);
```

---

## Architecture & Design

### Archetype Table Layout

All entities with an identical set of component types belong to the same **Archetype**. Component storage within each archetype is organized as **Structure of Arrays (SoA)**:

```text
Archetype [Position, Velocity, Health]:
Row 0:  [EntityID 0]   [Pos 0]   [Vel 0]   [Health 0]
Row 1:  [EntityID 1]   [Pos 1]   [Vel 1]   [Health 1]
Row 2:  [EntityID 2]   [Pos 2]   [Vel 2]   [Health 2]
```

Each component column is an independent, contiguous byte buffer. When an entity is attached or detached, it is migrated to the target archetype using swap-and-pop removal from the old archetype.

### Archetype Indexing (`inverted_map`)

Querying archetypes via `world.view<A, B>()` avoids linear searches through all archetypes. An inverted index maps component `TypeID`s to candidate archetypes, enabling fast superset intersection queries when matching signatures.

### Entity Handle Indirection

`byte::EntityID` consists of an integer index and generation counter. The pool maintains a sparse-set mapping each `EntityID` to its current `Location { Archetype*, row }`. When entities swap rows within an archetype during removals, their location records are updated in $O(1)$.

---

## Runtime Checks & Debugging

ByteECS includes internal safety assertions (e.g., verifying entity validity, preventing double-attaches, and bounds checks).

- **Debug builds** (`NDEBUG` not defined): checks are **enabled**.
- **Release builds** (`-DNDEBUG` defined): checks are **disabled**.

You can explicitly control checking by defining `BYTE_ECS_CHECKS`:

```bash
# Force enable checks even with -O3
clang++ -std=c++23 -O3 -DBYTE_ECS_CHECKS=1 -Isrc your_app.cpp

# Force disable checks
clang++ -std=c++23 -DBYTE_ECS_CHECKS=0 -Isrc your_app.cpp
```

---

## Public Aliases

Including `<ecs.hpp>` brings the `byte` namespace with the following aliases:

| Alias | Target Type | Description |
| :--- | :--- | :--- |
| `byte::EntityID` | `byte::ecs::EntityID` | 64-bit entity handle |
| `byte::Pool<Alloc>` | `byte::ecs::Pool<Alloc>` | Central ECS container |
| `byte::Archetype<Alloc>` | `byte::ecs::Archetype<Alloc>` | Storage table for single signature |
| `byte::Bulk<Pool>` | `byte::ecs::Bulk<Pool>` | Batch creation and mutation helper |
| `byte::Defer<Pool>` | `byte::ecs::Defer<Pool>` | Deferred command buffer queue |
| `byte::PoolView<...>` | `byte::ecs::PoolView<...>` | Multi-archetype query view |
| `byte::ArchetypeView<...>` | `byte::ecs::ArchetypeView<...>` | Single-archetype typed view |
| `byte::ComponentInfo` | `byte::ecs::ComponentInfo` | Component layout and lifecycle hooks |
| `byte::TypeID` | `byte::ecs::TypeID` | 64-bit stable type hash |
| `byte::TypeIdV<T>` | `byte::type_id_v<T>` | Compile-time component ID constant |
| `byte::ComponentInfoV<T>` | `byte::ecs::component_info_v<T>` | Compile-time component info constant |
| `byte::MakeArchetype<T...>()`| `byte::ecs::make_archetype<T...>()` | Factory helper for Archetypes |

---

## Roadmap

- [x] Archetype storage & SoA memory layout
- [x] Fast component queries (`view` / `each`)
- [x] Query filtering (`.with()` / `.without()`)
- [x] Inverted map signature indexing
- [x] Bulk operations
- [x] Deferred structural operations (`defer` / `flush`)
- [x] Performance benchmarks (`main.cpp`)
- [ ] Multi-threaded query execution / job system integration
- [ ] Continuous Integration (CI for Clang, GCC, MSVC)
- [ ] Serialization & state snapshotting

---

## Contributing

Contributions, bug reports, and pull requests are welcome! Please ensure that:

- Code conforms to **C++23** standards.
- Headers remain self-contained and header-only.
- New features adhere to the project's formatting conventions (`.clang-format`).

---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

# Physarum Simulation - User Guide & Documentation

A real time slime mold (Physarum polycephalum) simulation with multi species emergent behavior and an algorithm benchmark mode for comparing pathfinding algorithms.

---


## Building/running

### Prerequisites
- C++20 compatible compiler (g++ / clang++)
- SFML 3.x installed (brew install sfml on macOS)

### Build commands

Standard build:
bash
## Using vscode task
Shift+Cmd+B 

## Or manually:
g++ -std=c++20 -O3 -o bin/main -I headers source/*.cpp \
    -I /opt/homebrew/include -L /opt/homebrew/lib \
    -lsfml-graphics -lsfml-window -lsfml-system


Run:
bash
./bin/main


---

## What you'll see on first launch

When you first run the simulation, you'll see:

1. A black window (1200x900) with slime mold agents moving around
2. Yellow/colored trails left by the agents as they explore
3. HUD overlay (top left) showing:
   - Current agent count
   - Trail settings (decay, diffuse, weight)
   - Movement parameters
   - Current species mode
   - Keyboard controls

The simulation starts in the single species mode with ~10,000 yellow agents exhibiting classic Physarum behavior - following chemical trails, depositing pheromones, and self organizing into network like patterns.

---

## Species modes overview

Press M to cycle through four modes:

| Mode | Description |
|------|-------------|
| Single | One species (default yellow), classic slime mold behavior |
| Classic5 | 5-8 distinct species with unique personalities (Red=aggressive, Blue=cooperative, Green=loner, Yellow=chaotic, Magenta=orderly, + 3 special species) |
| Random | 1-10 randomly generated species with mixed behaviors |
| Algorithm benchmark | Special mode for comparing pathfinding algorithms |

---

## Basic controls

### Navigation & display
| Key | Action |
|-----|--------|
| M | Cycle through species modes (Single -> Classic5 -> Random -> *enchmark -> Single) |
| F | Cycle HUD mode (Full -> Compact -> Hidden) |
| Shift+F | Toggle ALL UI off/on instantly |
| Shift+1/2/3/4 | Move HUD position (TopLeft/TopRight/BottomLeft/BottomRight) |
| X | Toggle HUD transparency |

### Simulation control
| Key | Action |
|-----|--------|
| Space | Reset simulation (or start benchmark in benchmark mode) |
| up arrow/down arrow | Increase/decrease agent count (±10000 in normal mode) |
| S | Save settings to file |
| L | Load settings from file |

### Trail & Visual Settings
| Key | Action |
|-----|--------|
| 1/2 | Increase/decrease diffuse rate |
| 3/4 | Increase/decrease decay rate |
| 5/6 | Increase/decrease trail weight |
| T/Y | Adjust display threshold |
| B | Toggle blur effect |
| Shift+S | Toggle slime shading |
| Shift+A | Toggle anisotropic splats |

### Mouse Interaction
| Action | Effect |
|--------|--------|
| Left click | Deposit food/attractant (agents will be drawn to it) |
| Right click | Deposit repellent (agents will avoid it) |
| Hold & drag | Continuous drawing |
| Shift+up/down | Adjust mouse brush strength |

---

## Benchmark mode

### What is it?

The benchmark mode is just a visual pathfinding algorithm race where 7 different algorithms compete to navigate agents through a maze from left to right. It demonstrates:

1. How different search algorithms explore space (visually via colored trails)
2. Performance differences between uninformed vs. informed search
3. Empirical complexity analysis via doubling experiments

### How to enter benchmark mode

1. Press M repeatedly until you see "Benchmark" mode
   - The mode cycles: Single -> Classic5 -> Random -> Benchmark -> Single
2. You'll see:
   - A maze generated on the screen
   - Colored agents arranged in horizontal lanes (one color per algorithm)
   - Goal marker (yellow circle) on the right side
   - HUD panels showing timing and stats

### The 7 algos being compared

| Algorithm | Type | Color | Description |
|-----------|------|-------|-------------|
| DFS | Explorer (Uninformed) | Orange-red | Depth-first search. Explores deeply before backtracking |
| Dijkstra | Explorer (Uninformed) | Steel blue | Shortest path by distance. An octagonal wavefront expansion |
| Slime | Explorer (Biological) | Bright green | Simulated slime mold behavior. Chemical trail following |
| A* | Pathfinder (Informed) | Lime green | Classic heuristic search with f = g + h |
| Greedy | Pathfinder (Informed) | Pink | Heuristic only. Dumb but still goal aware |
| JPS | Pathfinder (Informed) | Cyan | Jump Point Search. Optimized A* for uniform grids |
| Theta* | Pathfinder (Informed) | Yellow | Any angle pathfinding. Smoother paths |

Explorer vs pathfinder:
- Explorers don't know where the goal is. They just search blindly until they find it
- Pathfinders know the goal location and use heuristics to find efficient paths.

---

## Benchmark mode controls

### Starting & controlling the race

| Key | Action |
|-----|--------|
| Space | Start the benchmark race (or reset if already running) |
| R | Regenerate the maze with new layout |
| D | Cycle maze difficulty (Level 1-6, affects cell count) |

### Adjusting agent count

| Key | Action |
|-----|--------|
| Up | Adds 25 agents per algorithm |
| Down | Removes 10 agents per algorithm |

### Toggling algorithms on/off

| Key | Algorithm |
|-----|-----------|
| Shift+1 | Toggle DFS |
| Shift+2 | Toggle Dijkstra |
| Shift+3 | Toggle Slime |
| Shift+4 | Toggle A* |
| Shift+5 | Toggle Greedy |
| Shift+6 | Toggle JPS |
| Shift+7 | Toggle Theta* |

This will let you run races with specific algorithm subsets for clearer comparisons if you want.

### Changing start positions

| Key | Action |
|-----|--------|
| P | Pack all agents into one random lane (tests fairness) |
| P (again) | Unpack - return agents to their original lanes |

---

## Understanding the display

### Left panel - race status


    Benchmark
    Time: 12.3s [ACTIVE]

    #1  [========  ] A*          45/50 (3.2s)

    #2  [====      ] Greedy      38/50 (3.8s)

    []           DFS         2/50

    []           Dijkstra    0/50

    True Maze - Level 3 (N = 192 cells)

    SPACE: Start | R: Regen | D: Difficulty | +/-: Agents
    P: Pack agents | Shift+1-7: Toggle algorithm


- Rank (#1, #2, etc.). Position in the race based on arrivals
- Progress bar. Visual of how many agents reached the goal
- Arrival count. Example: 45/50 means 45 of 50 agents finished
- First arrival time. (3.2s) = when first agent from that group reached goal
- Maze info. Current complexity level and cell count

### Right panel - empirical doubling results


Empirical Doubling
(pathfind compute time)

A*       r=1.89 O(n)
Greedy   r=1.72 O(n)
JPS      r=1.45 O(log n)
Theta*   r=2.12 O(n)

N = maze cells (48 -> 108 -> 192 -> 432 -> 768 -> 1728)
r = avg T(2N)/T(N) ratio across all 5 doublings


Shows algorithm complexity estimates based on timing:
- r (ratio) - How much slower the algorithm gets when problem size doubles
- Big-O estimate. Derived from the ratio:
  - r < 1.5 -> O(log n)
  - r < 2.5 -> O(n)
  - r < 3.5 -> O(n log n)
  - r < 5.0 -> O(n²)

Note: Naive's (DFS, Dijkstra, Slime) aren't in the doubling experiment because they don't compute deterministic paths, they just roam until finding the goal.

---

## The Point of all this

### Goals

1. A visualize algorithm behavior. See how DFS dives deep while Dijkstra expands uniformly, how A* uses heuristics to "aim" toward the goal and so on.

2. Compare informed vs uninformed search. Watch explorers wander while pathfinders go (mostly) straight when possible.

3. Understand complexity empirically. The doubling experiment shows that even when theory says O(n), real performance depends on constants, cache effects, and problem structure in this context.


### Observations

1. Theta* and JPS usually win of course. Informed search with good heuristics beats blind explorations that simple
2. Greedy is fast but risky. May make mistake or ignore optimal paths.
3. DFS creates distinctive patterns. This really cool "burning wool" like effect from deep exploration
4. Maze structure matters. Some mazes favor certain algorithms over others


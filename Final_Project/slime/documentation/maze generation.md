
# Maze generation notes
This is the written receipt for how the maze code in Pathfinder.cpp actually works.


## What these mazes are even for

Two very different use cases share the same generator:
1. **Benchmark demo runs** – throw obstacles down fast so the visualizer has something interesting to show while all the algorithms "race" in real time.
2. **Empirical doubling runs** – feed deterministic mazes with controlled cell counts into the timing harness so we can estimate how each algorithm scales.

Because of that split the code has two families: the quick generateMaze(float density) generator and the meticulous generateTrueMaze(int complexityLevel) backtracker. Same Pathfinder buffers (blocked_, obstacles_, gridWidth_, …) but different guarantees.

## Family 1 – generateMaze

This is the "draw walls until it looks interesting" path. It is deterministic per RNG seed but we let std::random_device pick the seed so every reset is a new maze layout naturally.

### Layout scaffolding

- Spawn and goal protection strips: spawnSafeZone = goalSafeZone = 8 grid cells  about 64px of empty space on each side. So agents spawn left of barrierLeft = spawnSafeZone + 1 and goal sits right of barrierRight = gridWidth_ - goalSafeZone - 1.
- Horizontal lid + floor: topWallThickness = 3 cells, gaps across middle third (topGapStart = h/3, topGapEnd = 2h/3). Keeps agents from cheeseing around the top/bottom.
- Vertical funnels on both ends tie those walls down so all the slimes are forced toward the maze interior part.

### Barrier math
- numBarriers = 5 + int(density * 3) so density in [0,1] produces 5–8 long horizontal bands.
- Each band gets numGaps = 2 + (gen() % 2) holes. Gap width is 6 + (gen() % 5) cells. The code slices the span into section = width / numGaps pieces and randomly offsets each gap inside its section so they do not all line up.
- Vertical clutter count: numVerticals = 6 + int(density * 6) and each column is 10–29 cells tall with 1–2 cells of thickness. start Y gets a slight random offset so the seams don't form straight tunnels.
- Scattered sticks: every 12-ish cells across, 1–3 vertical fragments appear with heights 3–8 cells. Think of them as stingers that break obvious diagonals.
- Blocks: density * 12 attempts, each picks a random (bx, by) and random 2–5 cell width/height and stamps a filled rectangle.

### Knobs / Failure modes

- Solvability isn't guaranteed here. generateMaze calls clearObstacles but never runs a flood fill verification step, and there isn't a built-in hotkey wired up for it at this moment.
- Because all counts derive from gridWidth_ / gridHeight_, a smaller render window means fewer pixels per cell and the exact same obstacle counts. When those counts get jammed into a tiny canvas you can end up with corridors only one cell wide, which is why tiny viewports feel unfair.
- This generator never tries to keep path complexity proportional it's just pure visual noise. Great for screenshots and watching it, but terrible for measuring algorithm scaling, so the doubling experiment ignores this mode and only uses generateTrueMaze in this current version anyways as of writing this.


## Family 2 – generateTrueMaze

generateTrueMaze is the controlled pipeline. It carves a perfect maze (spanning tree) inside the "playable" area, then optionally punches extra entrances and loops. Same machinery as the classic recursive backtracker but stretched to fit the render grid.

### grid math recap

- first we clamp complexityLevel to [1,6] and pick hardcoded cell counts:

| level | cells (x * y) | total cells | ratio vs previous |
|-------|---------------|-------------|-------------------|
| 1 | 8 * 6 | 48 | start |
| 2 | 12 * 9 | 108 | ~2.25* |
| 3 | 16 * 12 | 192 | ~1.78* |
| 4 | 24 * 18 | 432 | ~2.25* |
| 5 | 32 * 24 | 768 | ~1.78* |
| 6 | 48 * 36 | 1728 | ~2.25* |

- Note: cannot double exactly each level because the render target is ~300*225 cells (1200*900 pixels with 4px cell size). exact doubling would squeeze corridors below 3px wide by level 6, which fails collision checks. The stair step ratios keep corridors >=4px while still hovering around 2x input growth on average.
- mazeCellCount_ = mazeCellsX * mazeCellsY gets cached for HUD text and doubling math.
- cell -> pixel bounds use integer division so every pixel gets claimed exactly once:

cpp
// logical (cx, cy) -> pixel rectangle [x1,x2) * [y1,y2)
x1 = mazeLeft + (cx * mazeWidth) / mazeCellsX;
x2 = mazeLeft + ((cx + 1) * mazeWidth) / mazeCellsX;
// same idea for y


### Carving

1. Blanket fill everything to the right of spawnWidth with walls (blocked_[getIndex(x,y)] = true). Its cheaper to carve empty corridors than track where walls need to be.
2. carveCell(cx, cy) shrinks the cell bounds inward by wallThickness = 1 and clears the interior pixels. This ends up leaving a 1px shell so later passages have substance.
3. carvePassage(a, b) computes the overlapping strip between neighboring cells and clears a rectangle that straddles both shells. Horizontal neighbors expand overlap in X, vertical neighbors expand in Y, then both axes shrink by wallThickness so the doorway width matches cell interiors.
4. Classic DFS stack does the recursive backtracker. We seed at (0, mazeCellsY / 2) so the start lane lines up with the center spawn. Neighbors are the four cardinal offsets, randomly chosen via uniform_int_distribution each step.
5. Every carved edge gets shoved into passages (a set of normalized edge pairs) so we know later which walls remain intact for loop making.

### Entrances + exits

- The spawn lane gets opened by carving from spawnWidth - 1 into the first cell's shell. We repeat that for extra entrances when complexityLevel > 1.
- Formula: extraEntrances = complexityLevel - 1. We collect all rows except the main entrance, shuffle, and open from the spawn lane into that boundary cell. Good entrances (even index) also carve into (1, row) so they reach the maze. Bad entrances remain dead end alcoves to waste naive explorers time and confuse them.
- Exit is symmetrical: carve open the shell around (mazeCellsX - 1, mazeCellsY / 2) all the way to the right margin so the goal marker can sit inside a cleared pocket.

### Optional loops (suboptimal paths)

- extraPaths = max(0, complexityLevel - 1) as well. we walk every horizontal + vertical neighbor pair, collect those not in passages, shuffle, then knock out the first extraPaths walls. each removal adds exactly one cycle because we're starting from a tree.
- Cycle count check: cycles = |passages| - mazeCellCount_ + 1. Nice number to print when tuning.

### Doubling math hook

- The doubling experiment runs each deterministic pathfinder (A*, Greedy, JPS, Theta) on the true maze at every complexity level, three trials each, then compares timings. ratios plug into BenchmarkManager::estimateBigO using widened thresholds to ignore noise:

r = T(2N) / T(N)

if (ratio < 1.2) return "O(1)";        // caches or pure luck
if (ratio < 1.5) return "O(log n)";    // nearly flat
if (ratio < 2.5) return "O(n)";        // linear-ish
if (ratio < 3.5) return "O(n log n)";  // mild superlinear
if (ratio < 5.0) return "O(n^2)";      // quadratic vibes
return "O(n^2+)";                      // worse


- Because our cell ratios bounce between 1.78* and 2.25* the interpretation isn't literal "doubling" but close enough to show slope trends on the HUD.


Collision + rendering

- blocked_ is the only source of truth for passability. After generation we convert it into draw friendly rectangles via row-wise run-length encoding—each contiguous horizontal run of blocked cells becomes {x, y, width, 1} and gets pushed into obstacles_ for SFML to draw.


Other generators (parked)

generateAdvancedMaze dispatches to labyrinth, multi-path, bottleneck, spiral, and chambers variants. They share spawn/goal margins but aren't used by the benchmark right now only generateTrueMaze feeds the doubling experiment.


References

- https://en.wikipedia.org/wiki/Maze_generation_algorithm#Recursive_backtracker
- https://weblog.jamisbuck.org/2010/12/27/maze-generation-recursive-backtracking
- https://www.kufunda.net/publicdocs/Mazes%20for%20Programmers%20Code%20Your%20Own%20Twisty%20Little%20Passages%20(Jamis%20Buck).pdf
- https://www.redblobgames.com/pathfinding/grids/algorithms.html


tl;dr

| thing | why it exists |
|-------|----------------|
| generateMaze(density) | fast noisy obstacle fields for visual chaos |
| generateTrueMaze(level) | controlled problem sizes for benchmark math |
| carveCell / carvePassage | map logical cells to pixel perfect corridors |
| extra entrances & loops | adjustable difficulty knobs tied to level |
| RLE obstacle export | just a cheap way to turn blocked_ bits into SFML rectangles |


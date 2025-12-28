# Table of Contents
- [Algorithms Overview](#high-overview-of-algorithms)
- [Emperical Vs Theoretical Analysis](#emperical-vs-theoretical-analysis)
- [Caveats / Technical Assumptions](#limitations-and-caveats)
- [Doubling Method Algorithm Choice Justification](#note-on-why-explorers-uniform-searchers-naive-algos-etc-arent-in-doubling)

## High overview of Algorithms

1. **Dijkstra’s Algorithm** – the classic non heuristic finds the shortest path on weighted graphs. Slow but reliable.

2. **A\*** – The superstar. That uses heuristics (like euclidean or manhattan distance) to speed up dijkstra.

3. **Breadth First Search (BFS)** – Unweighted graphs only. Finds the shortest path in terms of steps, not cost.

4. **Depth First Search (DFS)** – Not efficient for shortest path, but it technically counts as pathfinding in like an exploration context I guess.

5. **Greedy Best First Search** – Like A* but only cares about the heuristic, not the cost so far. Often pretty stupid but fast.

6. **Bidirectional Search** – Runs search forward from start and backward from goal hoping they meet in the middle. This is still in progress on Ethan's branch.

7. **Jump Point Search (JPS)** – Optimization of A* for uniform grids cuts down on redundant nodes.

8. **D\* Lite** – Dynamic A*. Originally designed for robotics where the map changes as you explore. Maybe drop this one? This has not yet been implemented

9.  **Theta\*** – Variant of A* that allows any angle paths instead of restricting to grid connections.

10. **Floyd–Warshall** – All pairs shortest paths. Less about “find one path now” and more about “know every possible path always.” This has not yet been implemented. 

## Emperical Vs. Theoretical Analysis
This document compares the **theoretical worst-case** time complexities of several pathfinding algorithms (DFS, Dijkstra, A\*, JPS, Theta\*, Greedy) with their **empirical scaling behavior** measured using the doubling method on maze sizes:

**48, 108, 192, 432, 768, 1728 cells**

The empirical metric is the **doubling ratio**: T(2N)/T(N) with the average taken across five approximate doublings.

### 1. How to Interpret the Ratios

- \( k = 1 \) -> linear time  
- \( k < 1 \) -> sublinear scaling  
- \( k = 0 \) -> constant-time (in practice: dominated by noise)  
- \( k > 1 \) -> superlinear  

For an O(Nlog N) algorithm, the expected doubling ratio for the maze sizes is approximately **2.24** which comes from evaluating 2 * log(2N)/log(N) at the specified N values.


### 2. Theoretical Worst-Case Complexities

Assume grid graphs with:

- \( V = N \) nodes  
- \( E = Theta(N) \) edges  
- Binary heap priority queues 

| Algorithm | Worst-Case Complexity | Notes |
|----------|------------------------|-------|
| DFS / BFS | O(N) | linear with visited tracking |
| Dijkstra | O(N log N) | priority queue dominant |
| A\* | O(N log N) | worst case behaves like Dijkstra |
| Greedy Best-First | O(N log N) | highly heuristic-dependent |
| JPS | O(N log N) | worst case equal to A* |
| Theta\* | O(N log N) | possible A*-like behavior |

### 3. Empirical Results

#### Average doubling ratios (r)

- **A\***: 1.03  
- **Greedy**: 1.23  
- **JPS**: 1.31  
- **Theta\***: 0.99  

Guesstimated exponent conversion where we approximated k = log2(r)

| Algorithm | r | k = log2(r) | Interpretation |
|----------|----|------------|----------------|
| **A\*** | 1.03 | 0.043 | Almost constant-time growth |
| **Greedy** | 1.23 | 0.299 | Sublinear scaling |
| **JPS** | 1.31 | 0.390 | Sublinear but steeper |
| **Theta\*** | 0.99 | −0.014 | Essentially constant; dominated by noise |


### 4. Per-Algorithm Interpretation

#### **A\***
- **Theory:** O(N log N) 
- **Empirical:** r = 1.03 , nearly constant  
- **Reason:** The heuristic restricts expansions to a thinner corridor on the maze. Constant factors of 2d grid and heuristic dominates.


#### **Greedy Best-First**
- **Theory:** O(N log N) 
- **Empirical:** r = 1.23, sublinear  
- **Reason:** Uses only heuristic distance, so explores more than A*, but still gains from good heuristics in these mazes. Sensitive to maze structure, run the simulation and see greedy go down a route but quickly turn backwards.


#### **JPS**
- **Theory:** O(N log N)
- **Empirical:** r = 1.31, sublinear, but gets worse with other simulations 
- **Reason:** Prunes intermediate nodes, but corridor-heavy mazes can produce many jump points. Overhead per “jump” increases with maze scale. Simulation did not allow for physical jumping.


#### **Theta\***
- **Theory:** O(N log N) 
- **Empirical:** r = 0.99
- **Reason:** Line-of-sight shortcuts bypass intermediate nodes so effectively that runtime barely changes across these N values. Negative exponent is measurement noise.


### 5. Why Empirical Results Are Better/Worse Than Theory

1. **Heuristic dominance**  
   A\* and Theta\* reduce expansions drastically on structured mazes.

2. **Maze topology**  
   Long corridors and sparse branching strongly favor heuristic search and JPS/Theta pruning.

3. **Small problem sizes**  
   48–1728 nodes is not large enough for asymptotic behavior. Constant overhead, caching, and branching patterns dominate.

4. **Measurement noise**  
   3-run averages + approximate doubling means that noisy r values exist, especially when the true scaling is flat.


### 6. Summary Table

| Algorithm | Worst-Case | Empirical r | k (log2 r) | Practical Behavior |
|----------|------------|-------------|------------|--------------------|
| **A\*** | O(N log N) | 1.03 | 0.043 | Essentially constant |
| **Greedy** | O(N log N) | 1.23 | 0.299 | Sublinear growth |
| **JPS** | O(N log N) | 1.31 | 0.390 | Sublinear but steeper |
| **Theta\*** | O(N log N) | 0.99 | −0.014 | Constant / noise |


### 7. Final Conclusion

**Worst-case theory predicts O(Nlog N) for A\*, Greedy, JPS, and Theta\*.  
Empirically, based on maze generator, all four scale *better* than that, but only because of heursitics.**

- A\* and Theta\* behave almost like **O(1)** across sample range.  
- Greedy and JPS grow **sublinearly** with exponents k = 0.3-0.4.  
- No algorithm shows behavior approaching its worst-case NlogN growth.

These results reflect strong heuristic guidance, aggressive pruning (Theta*/JPS), and the specific maze topology—not a contradiction of theoretical complexity.


## Limitations and caveats
The doubling method gives a practical "empirical" estimate not a rigorous proof of complexity:

1. **Approximate doubling (not exact 2x).** The maze dimensions progress as 48 -> 108 -> 192 -> 432 -> 768 -> 1728 cells, giving ratios of ~2.25x, ~1.78x, ~2.25x, ~1.78x, ~2.25x between levels. This averages to roughly 2x per level but isn't mathematically perfect obviously. "Why not use exact 2x scaling?" Well... because the maze is carved into a fixed resolution pixel grid (~300x225 for a 1200x900 window). With exact doubling (e.g. 32x48 = 1536 cells at level 6), each maze cell would shrink to ~9x5 pixels. After subtracting 1-pixel walls on each side, corridors become only 2-3 pixels wide, way way too narrow for the recursive backtracking carve to create connected passages. The current dimensions keep corridors at ~4+ pixels, which is the minimum for reliable pathfinding. Shrinking agent size wouldn't help: the blocked_[] collision grid determines passability, not agent sprites. So if a corridor isn't carved wide enough, no path even exists regardless of agent size.

2. **Maze structure vs. cell count.** The algorithms don't care about raw cell count, they care about nodes expanded, path length, and branching factor. A 16x12 maze isn't simply "2x harder" than 8x12; the maze topology (dead ends, corridor shapes, whatever) affects each algorithm differently.

3. **Small problem sizes.** At 48–1728 cells, constant factors and cache effects can dominate. True asymptotic behavior emerges more clearly at larger N (thousands to millions of nodes).

3. **Limited trials.** I'm averaging over 3 runs per level. OS scheduling, cache state, and other noise is gonna skew individual measurements. So production benchmarks typically use 100+ trials and discard outliers.

4. **Threshold buckets are heuristic.** The ratio to big O mapping (e.g. "ratio < 2.5 -> O(n)") uses widened ranges to tolerate measurement noise. These are reasonable estimates, meaning they are not exact classifications.

> The results are useful for comparing how algorithms scale *on this maze generator* and for the educational demonstration. But thats really it.



## Note on why explorers (uniform searchers, naive algos, etc.) aren't in doubling
- DFS as explorer, Dijkstra explorers, and the slime mold agents do not call a deterministic findPath(start, goal) that returns a costed path. they roam using local rules (stack/queue growth, chemical trails, etc.).
- Because there is no stable compute time per maze instance timing them would just measure how long they wander not algorithmic path computation.
- The empirical doubling requirement is about comparing theoretical vs. measured *pathfinding* complexity, so we're limiting the experiment to the deterministic goal directed algorithms (A*, Greedy, JPS, Theta). Explorers remain in benchmark mode purely for visual/qualitative comparison.
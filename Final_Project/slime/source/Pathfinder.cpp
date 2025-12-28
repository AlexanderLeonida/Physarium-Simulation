#include "Pathfinder.h"
#include <limits>
#include <random>
#include <iostream>
#include <set>

Pathfinder::Pathfinder(int width, int height, int cellSize)
    : worldWidth_(width), worldHeight_(height), cellSize_(cellSize) {
    // translate pixel space dimensions into grid space counts (ceil division keeps edges covered)
    gridWidth_ = (width + cellSize - 1) / cellSize;
    gridHeight_ = (height + cellSize - 1) / cellSize;
    blocked_.resize(gridWidth_ * gridHeight_, false);
}

void Pathfinder::resize(int width, int height) {
    // recomputing world/grid dimensions and reset obstacle buffers when the render target changes
    worldWidth_ = width;
    worldHeight_ = height;
    gridWidth_ = (width + cellSize_ - 1) / cellSize_;
    gridHeight_ = (height + cellSize_ - 1) / cellSize_;
    blocked_.assign(gridWidth_ * gridHeight_, false);
    obstacles_.clear();
}

void Pathfinder::setCellSize(int size) {
    cellSize_ = size;
    resize(worldWidth_, worldHeight_);
}

void Pathfinder::clearObstacles() {
    std::fill(blocked_.begin(), blocked_.end(), false);
    obstacles_.clear();
}

void Pathfinder::addObstacle(int x, int y, int width, int height) {
    Obstacle obs{x, y, width, height};
    obstacles_.push_back(obs);
    
    // marking cells as blocked so
    // we iterate over every cell inside the obstacles bounding rectangle and
    // flip the corresponding entry in blocked_ to true so that pathfinding
    // treats these coordinates as impassable
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int gx = x + dx;  // absolute grid X
            int gy = y + dy;  // absolute grid Y
            if (gx >= 0 && gx < gridWidth_ && gy >= 0 && gy < gridHeight_) {
                blocked_[getIndex(gx, gy)] = true;
            }
        }
    }
}

void Pathfinder::addObstacle(const Obstacle& obs) {
    addObstacle(obs.x, obs.y, obs.width, obs.height);
}

void Pathfinder::generateRandomObstacles(int count, int minSize, int maxSize, int marginLeft, int marginRight, bool clearExisting) {
    if (clearExisting) {
        clearObstacles();
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // leave margins on left (start) and right (goal)
    int safeLeft = marginLeft / cellSize_;
    int safeRight = gridWidth_ - (marginRight / cellSize_);
    
    if (safeRight <= safeLeft + 10) {
        // not enough space for obstacles
        return;
    }
    
    // random obstacle footprints are chosen entirely within the safe corridor.
    std::uniform_int_distribution<> xDist(safeLeft, safeRight - maxSize);
    std::uniform_int_distribution<> yDist(2, gridHeight_ - maxSize - 2);
    std::uniform_int_distribution<> sizeDist(minSize, maxSize);
    
    for (int i = 0; i < count; i++) {
        int ox = xDist(gen);
        int oy = yDist(gen);
        int ow = sizeDist(gen);
        int oh = sizeDist(gen);
        
        addObstacle(ox, oy, ow, oh);
    }
}

void Pathfinder::generateMaze(float density) {
    clearObstacles();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> prob(0.0, 1.0);
    
    // safe zones - leave room for spawn (left) and goal (right)
    // spawn margin is 50 pixels, cell size is 8 so need ~7 cells clear on left
    // goal margin is 50 pixels so need ~7 cells clear on right
    int spawnSafeZone = 8;  // 8 cells = 64 pixels - safe spawn area
    int goalSafeZone = 8;   // 8 cells = 64 pixels - safe goal area
    
    int safeLeft = spawnSafeZone;
    int safeRight = gridWidth_ - goalSafeZone;
    
    // barriers start just after spawn zone but leave tiny margin
    int barrierLeft = spawnSafeZone + 1;  // start barriers after spawn zone
    int barrierRight = gridWidth_ - goalSafeZone - 1;  // end barriers before goal
    
    // block top and bottom bypass routes
    // create the walls along the top edge (leaving only middle open of course)
    int topWallThickness = 3;
    int topGapStart = gridHeight_ / 3;  // gap starts at 1/3 from top
    int topGapEnd = gridHeight_ * 2 / 3;  // gap ends at 2/3 from top
    
    for (int x = barrierLeft; x < barrierRight; x++) {
        // top wall - blocks top bypass
        for (int y = 0; y < topWallThickness; y++) {
            if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                blocked_[getIndex(x, y)] = true;
            }
        }
        // bottom wall - blocks bottom bypass  
        for (int y = gridHeight_ - topWallThickness; y < gridHeight_; y++) {
            if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                blocked_[getIndex(x, y)] = true;
            }
        }
    }
    obstacles_.push_back({barrierLeft, 0, barrierRight - barrierLeft, topWallThickness});
    obstacles_.push_back({barrierLeft, gridHeight_ - topWallThickness, barrierRight - barrierLeft, topWallThickness});
    
    // vertical walls on left and right that funnel into the maze
    // left funnel wall - extends from spawn zone leaving gap in middle
    for (int y = 0; y < topGapStart; y++) {
        if (barrierLeft >= 0 && barrierLeft < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierLeft, y)] = true;
        }
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        if (barrierLeft >= 0 && barrierLeft < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierLeft, y)] = true;
        }
    }
    obstacles_.push_back({barrierLeft, 0, 1, topGapStart});
    obstacles_.push_back({barrierLeft, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    // right funnel wall - extends before goal zone again leaves gap in middle
    for (int y = 0; y < topGapStart; y++) {
        if (barrierRight >= 0 && barrierRight < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierRight, y)] = true;
        }
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        if (barrierRight >= 0 && barrierRight < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierRight, y)] = true;
        }
    }
    obstacles_.push_back({barrierRight, 0, 1, topGapStart});
    obstacles_.push_back({barrierRight, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    //internal maze barriers
    // horizontal barriers with gaps these span most of the width
    int numBarriers = 5 + static_cast<int>(density * 3);  // 5-8 main barriers
    // barriers only in the middle section (between top/bottom walls)
    int mazeTop = topWallThickness + 2;
    int mazeBottom = gridHeight_ - topWallThickness - 2;
    int spacing = (mazeBottom - mazeTop - 10) / (numBarriers + 1);
    
    for (int i = 1; i <= numBarriers; i++) {
        int barrierY = mazeTop + 5 + i * spacing;
        
        // create 2-3 gaps per barrier (narrower gaps = harder)
        int numGaps = 2 + (gen() % 2);  // 2-3 gaps
        std::vector<std::pair<int, int>> gaps;
        
        for (int g = 0; g < numGaps; g++) {
            int gapWidth = 6 + (gen() % 5);  // gap of 6-10 cells (narrower)
            int section = (barrierRight - barrierLeft) / numGaps;
            int gapStart = barrierLeft + g * section + (gen() % std::max(1, section - gapWidth));
            gaps.push_back({gapStart, gapStart + gapWidth});
        }
        
        // the barrier with gaps - extend to screen edges
        int thickness = 1 + (gen() % 2);  // 1-2 cells thick
        for (int t = 0; t < thickness; t++) {
            int y = barrierY + t;
            if (y >= gridHeight_ - 2) continue;
            
            for (int x = barrierLeft; x < barrierRight; x++) {
                // check for if in any gap
                bool inGap = false;
                for (auto& gap : gaps) {
                    if (x >= gap.first && x < gap.second) {
                        inGap = true;
                        break;
                    }
                }
                if (inGap) continue;
                
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        
        // record segments for rendering
        int segStart = barrierLeft;
        for (auto& gap : gaps) {
            if (gap.first > segStart) {
                obstacles_.push_back({segStart, barrierY, gap.first - segStart, thickness});
            }
            segStart = gap.second;
        }
        if (segStart < barrierRight) {
            obstacles_.push_back({segStart, barrierY, barrierRight - segStart, thickness});
        }
    }
    
    // for adding more vertical obstacles to create winding channels
    int numVerticals = 6 + static_cast<int>(density * 6);  // 6-12 vertical obstacles
    for (int v = 0; v < numVerticals; v++) {
        if (prob(gen) < density) {
            int vx = barrierLeft + 5 + (gen() % std::max(1, barrierRight - barrierLeft - 10));
            int startY = mazeTop + 2 + (gen() % 15);
            int height = 10 + (gen() % 20);  // 10-29 cells tall
            
            // add some thickness variation
            int thickness = 1 + (gen() % 2);
            for (int t = 0; t < thickness; t++) {
                int x = vx + t;
                if (x >= barrierRight) continue;
                for (int y = startY; y < std::min(startY + height, mazeBottom - 2); y++) {
                    if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                        blocked_[getIndex(x, y)] = true;
                    }
                }
            }
            obstacles_.push_back({vx, startY, thickness, std::min(height, mazeBottom - 2 - startY)});
        }
    }
    
    // short wall segments scattered throughout
    for (int x = barrierLeft + 5; x < barrierRight - 5; x += 12) {
        if (prob(gen) < density * 0.5f) {
            int numSegments = 1 + (gen() % 3);  // 1-3 segments per column
            std::uniform_int_distribution<> yDist(mazeTop + 3, mazeBottom - 8);
            
            for (int seg = 0; seg < numSegments; seg++) {
                int segY = yDist(gen);
                int segHeight = 3 + (gen() % 6);  // 3-8 cells tall
                int xOffset = (gen() % 3) - 1;
                int actualX = std::clamp(x + xOffset, barrierLeft + 1, barrierRight - 2);
                
                for (int y = segY; y < std::min(segY + segHeight, mazeBottom - 1); y++) {
                    if (actualX >= 0 && actualX < gridWidth_ && y >= 0 && y < gridHeight_) {
                        blocked_[getIndex(actualX, y)] = true;
                    }
                }
                obstacles_.push_back({actualX, segY, 1, std::min(segHeight, mazeBottom - 1 - segY)});
            }
        }
    }
    
    // scattered blocks - inside the maze bounds
    int numBlocks = static_cast<int>(density * 12);
    std::uniform_int_distribution<> blockXDist(barrierLeft + 3, barrierRight - 6);
    std::uniform_int_distribution<> blockYDist(mazeTop + 2, mazeBottom - 6);
    std::uniform_int_distribution<> blockSizeDist(2, 5);
    
    for (int i = 0; i < numBlocks; i++) {
        int bx = blockXDist(gen);
        int by = blockYDist(gen);
        int bw = blockSizeDist(gen);
        int bh = blockSizeDist(gen);
        
        for (int y = by; y < by + bh && y < mazeBottom; y++) {
            for (int x = bx; x < bx + bw && x < barrierRight; x++) {
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        obstacles_.push_back({bx, by, bw, bh});
    }
}


// maze generation
void Pathfinder::generateAdvancedMaze(MazeType type, float difficulty) {
    // standard margins for spawn/goal areas
    int spawnMargin = 8;  // cells for spawn area
    int goalMargin = 8;   // cells for goal area
    
    switch (type) {
        case MazeType::Simple:
            generateMaze(difficulty);
            break;
        case MazeType::Labyrinth:
            generateLabyrinthMaze(spawnMargin, goalMargin, difficulty);
            break;
        case MazeType::MultiPath:
            generateMultiPathMaze(spawnMargin, goalMargin, difficulty);
            break;
        case MazeType::Bottleneck:
            generateBottleneckMaze(spawnMargin, goalMargin, difficulty);
            break;
        case MazeType::Spiral:
            generateSpiralMaze(spawnMargin, goalMargin, difficulty);
            break;
        case MazeType::Chambers:
            generateChambersMaze(spawnMargin, goalMargin, difficulty);
            break;
        case MazeType::TrueMaze:
            // Use difficulty to determine complexity level (1-4)
            generateTrueMaze(1 + static_cast<int>(difficulty * 3));
            break;
    }
}


// PERFECT maze for empirical doubling experiments
// uses recursive backtracking to create a proper maze with corridors
// complexity level 1-6 determines maze cell count and corridor tightness

// TODO: add moving obstacles next time or maybe real time shuffling obstacles or rearranging maze?
/*
helpful stuff :) 
https://en.wikipedia.org/wiki/Maze_generation_algorithm#Recursive_backtracker
https://weblog.jamisbuck.org/2010/12/27/maze-generation-recursive-backtracking
https://www.kufunda.net/publicdocs/Mazes%20for%20Programmers%20Code%20Your%20Own%20Twisty%20Little%20Passages%20(Jamis%20Buck).pdf
https://www.redblobgames.com/pathfinding/grids/algorithms.html

:)
*/
void Pathfinder::generateTrueMaze(int complexityLevel) {
    clearObstacles();
    
    complexityLevel = std::clamp(complexityLevel, 1, 6);
    
    int spawnWidth = 10;
    int goalWidth = 8;
    
    // maze dimensions chosen to roughly double cell count each level while keeping corridors
    // wide enough to actually navigate. perfect 2x scaling would require cells too small to carve.
    // actual ratios: 2.25x, 1.78x, 2.25x, 1.78x, 2.25x — averages to ~2x per level.
    // this is a practical compromise: exact doubling would shrink corridors to 1-2 pixels.
    int mazeCellsX, mazeCellsY;
    switch (complexityLevel) {
        case 1: mazeCellsX = 8;  mazeCellsY = 6;  break;  // 48 cells
        case 2: mazeCellsX = 12; mazeCellsY = 9;  break;  // 108 cells (~2.25x)
        case 3: mazeCellsX = 16; mazeCellsY = 12; break;  // 192 cells (~1.78x)
        case 4: mazeCellsX = 24; mazeCellsY = 18; break;  // 432 cells (~2.25x)
        case 5: mazeCellsX = 32; mazeCellsY = 24; break;  // 768 cells (~1.78x)
        case 6: mazeCellsX = 48; mazeCellsY = 36; break;  // 1728 cells (~2.25x)
        default: mazeCellsX = 8; mazeCellsY = 6; break;
    }
    
    mazeCellCount_ = mazeCellsX * mazeCellsY;
    
    // defines the exact bounds to fill the space completely
    int mazeLeft = spawnWidth;
    int mazeRight = gridWidth_ - goalWidth;
    int mazeTop = 0;
    int mazeBottom = gridHeight_;
    int mazeWidth = mazeRight - mazeLeft;
    int mazeHeight = mazeBottom - mazeTop;
    
    int wallThickness = 1;
    
    // blanket block the maze area so later carving operations only have to clear corridors
    for (int y = 0; y < gridHeight_; y++) {
        for (int x = spawnWidth; x < gridWidth_; x++) {
            blocked_[getIndex(x, y)] = true;
        }
    }
    
    // helper to get cell bounds using integer distribution to avoid the gaps
    // given logical maze cell (cx, cy) it computes the pixel space bounding box
    // (x1,y1) to (x2,y2). integer math guarantees cells tile perfectly
    auto getCellBounds = [&](int cx, int cy, int& x1, int& y1, int& x2, int& y2) {
        x1 = mazeLeft + (cx * mazeWidth) / mazeCellsX;       // left edge
        y1 = mazeTop + (cy * mazeHeight) / mazeCellsY;       // top edge
        x2 = mazeLeft + ((cx + 1) * mazeWidth) / mazeCellsX; // right edge (exclusive)
        y2 = mazeTop + ((cy + 1) * mazeHeight) / mazeCellsY; // bottom edge (exclusive)
    };

    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<std::vector<bool>> visited(mazeCellsX, std::vector<bool>(mazeCellsY, false));
    
    auto carveCell = [&](int cx, int cy) {
        // clearing the interior of a single maze cell minus the outer wall thickness buffer
        int bx1, by1, bx2, by2;
        getCellBounds(cx, cy, bx1, by1, bx2, by2);
        
        // then shrink bounds inward by wallThickness so walls remain around the cell
        int x1 = bx1 + wallThickness;
        int y1 = by1 + wallThickness;
        int x2 = bx2 - wallThickness;
        int y2 = by2 - wallThickness;
        
        // safety clamp here: so at least 1 pixel carved even if cell is tiny
        if (x2 <= x1) x2 = x1 + 1;
        if (y2 <= y1) y2 = y1 + 1;
        
        // looping through every pixel in the interior and mark it passable
        for (int y = y1; y < y2; y++)
            for (int x = x1; x < x2; x++)
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_)
                    blocked_[getIndex(x, y)] = false;
    };
    
    auto carvePassage = [&](int cx1, int cy1, int cx2, int cy2) {
        // opens a doorway between neighboring cells by clearing their overlapping boundary.
        // get pixel bounds for both cells
        int b1x1, b1y1, b1x2, b1y2;
        int b2x1, b2y1, b2x2, b2y2;
        getCellBounds(cx1, cy1, b1x1, b1y1, b1x2, b1y2);
        getCellBounds(cx2, cy2, b2x1, b2y1, b2x2, b2y2);
        
        // computing where the two cells share an edge (the wall strip between them)
        int overlapX1 = std::max(b1x1, b2x1);
        int overlapX2 = std::min(b1x2, b2x2);
        int overlapY1 = std::max(b1y1, b2y1);
        int overlapY2 = std::min(b1y2, b2y2);
        
        // expand overlap to actually cover the wall pixels separating the cells
        if (cx1 != cx2) {
            // horizontal neighbors (side by side): widen X span.
            overlapX1 = std::min(b1x2, b2x2) - 2;
            overlapX2 = std::max(b1x1, b2x1) + 2;
        } else {
            // vertical neighbors (one above the other): widen Y span.
            overlapY1 = std::min(b1y2, b2y2) - 2;
            overlapY2 = std::max(b1y1, b2y1) + 2;
        }
        
        // shrink inward by wallThickness so the corridor isnt wider than it should be
        int carveX1 = overlapX1 + wallThickness;
        int carveX2 = overlapX2 - wallThickness;
        int carveY1 = overlapY1 + wallThickness;
        int carveY2 = overlapY2 - wallThickness;
        
        // and a fallback if bounds inverted (tiny cells).
        if (carveX2 <= carveX1) { carveX1 = overlapX1; carveX2 = overlapX2; }
        if (carveY2 <= carveY1) { carveY1 = overlapY1; carveY2 = overlapY2; }
        
        // clear the rectangle, making the passage walkable.
        for (int y = carveY1; y < carveY2; y++)
            for (int x = carveX1; x < carveX2; x++)
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_)
                    blocked_[getIndex(x, y)] = false;
    };

    // canonical edge representation for passages/walls.
    // note: an edge is just a pair of cell coordinates (cell1, cell2) 
    using Edge = std::pair<std::pair<int,int>, std::pair<int,int>>;
    // normEdge makes sure that the smaller coordinate pair comes first... so two.
    // opposite order calls produce the same key for set/map lookups
    auto normEdge = [](int x1, int y1, int x2, int y2) -> Edge {
        Edge e{{x1, y1}, {x2, y2}};
        if (e.second < e.first) std::swap(e.first, e.second);
        return e;
    };
    
    // track which passages exist (for adding extra paths later)
    std::set<Edge> passages;
    
    // recursive backtracking
    std::vector<std::pair<int, int>> stack;
    // depth first carve that visits each cell exactly once (classic recursive backtracker)
    int startX = 0;
    int startY = mazeCellsY / 2;
    
    visited[startX][startY] = true;
    stack.push_back({startX, startY});
    carveCell(startX, startY);
    
    // then open entrance from spawn
    int bx1, by1, bx2, by2;
    getCellBounds(startX, startY, bx1, by1, bx2, by2);
    for (int y = by1 + wallThickness; y < by2 - wallThickness; y++) {
        for (int x = spawnWidth - 1; x < bx1 + wallThickness; x++) {
             if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_)
                blocked_[getIndex(x, y)] = false;
        }
    }
    
    // main recursive backtracking loop.
    while (!stack.empty()) {
        auto [cx, cy] = stack.back();  // peek current cell.
        
        // gather unvisited orthogonal neighbors (up/down/left/right)
        std::vector<std::pair<int, int>> neighbors;
        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};
        
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            
            // only consider in bounds cells that havent been carved yet 
            if (nx >= 0 && nx < mazeCellsX && ny >= 0 && ny < mazeCellsY && !visited[nx][ny]) {
                neighbors.push_back({nx, ny});
            }
        }
        
        if (!neighbors.empty()) {
            // random neighbor thats mark as visited and pushed onto the stack
            std::uniform_int_distribution<> dist(0, neighbors.size() - 1);
            auto [nx, ny] = neighbors[dist(gen)];
            
            visited[nx][ny] = true;
            stack.push_back({nx, ny});
            
            // carve the interior of the new cell and punch a doorway from current to it
            carveCell(nx, ny);
            carvePassage(cx, cy, nx, ny);
            passages.insert(normEdge(cx, cy, nx, ny));  // track that opened passage
        } else {
            // dead end: backtrack to previous cell
            stack.pop_back();
        }
    }

    // suboptimal paths (cycles) based on complexity level
    // level 1: 0 extra paths (Perfect Maze)
    // level N: N-1 extra paths
    // additional entrances: level 1 = 1 entrance. level N = N entrances (start + N-1 extras)
    int extraEntrances = std::max(0, complexityLevel - 1);
    if (extraEntrances > 0) {
        std::vector<int> entranceRows;
        entranceRows.reserve(mazeCellsY - 1);
        for (int r = 0; r < mazeCellsY; ++r) {
            if (r == startY) continue; // keeping the primary entrance untouched
            entranceRows.push_back(r);
        }
        std::shuffle(entranceRows.begin(), entranceRows.end(), gen);
        int made = 0;
        for (int row : entranceRows) {
            if (made >= extraEntrances) break;
            int bx1, by1, bx2, by2;
            getCellBounds(0, row, bx1, by1, bx2, by2);
            int y1 = by1 + wallThickness;
            int y2 = by2 - wallThickness;
            if (y2 <= y1) y2 = y1 + 1;
            // open from spawn lane into this boundary cell
            for (int y = y1; y < y2; ++y) {
                for (int x = spawnWidth - 1; x < bx1 + wallThickness; ++x) {
                    if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_)
                        blocked_[getIndex(x, y)] = false;
                }
            }
            // half "good" (connected) half "bad" (dead end)
            bool goodEntrance = (made % 2 == 0);
            if (goodEntrance) {
                // connect into maze via passage to the neighbor cell (1, row)
                carvePassage(0, row, 1, row);
                passages.insert(normEdge(0, row, 1, row));
            }
            // and for bad entrances we leave the wall to the maze in place making a short dead end
            made++;
        }
    }

    int extraPaths = std::max(0, complexityLevel - 1);
    
    if (extraPaths > 0) {
        // builds list of walls (adjacent cell pairs with no passage just yet)
        std::vector<Edge> walls;
        std::vector<Edge> extraOpened;
        
        // collect all walls (potential extra openings) that are NOT already passages
        // scan horizontally adjacent cell pairs...
        for (int y = 0; y < mazeCellsY; y++) {
            for (int x = 0; x < mazeCellsX - 1; x++) {
                Edge e = normEdge(x, y, x + 1, y);
                if (passages.find(e) == passages.end()) {
                    walls.push_back(e);
                }
            }
        }
        
        // ...and vertically adjacent cell pairs
        for (int x = 0; x < mazeCellsX; x++) {
            for (int y = 0; y < mazeCellsY - 1; y++) {
                Edge e = normEdge(x, y, x, y + 1);
                if (passages.find(e) == passages.end()) {
                    walls.push_back(e);
                }
            }
        }
        
        std::cout << "   -> Found " << walls.size() << " intact walls." << std::endl;
        
        // a randomly shuffle and pick a subset of walls to knock out which creates loops.
        std::shuffle(walls.begin(), walls.end(), gen);
        
        int added = 0;
        for (const auto& wall : walls) {
            if (added >= extraPaths) break;
            // open the wall by carving a passage between these two cells
            carvePassage(wall.first.first, wall.first.second, wall.second.first, wall.second.second);
            passages.insert(wall);
            extraOpened.push_back(wall);
            added++;
        }
        // cycle count estimate: edges - vertices + connected components (1 for a single maze).
        int cycleCount = static_cast<int>(passages.size()) - mazeCellCount_ + 1;
        std::cout << "   -> Added " << added << " suboptimal paths (cycles)." << std::endl;
    }
    
    // open exit to goal
    int endX = mazeCellsX - 1;
    int endY = mazeCellsY / 2; 
    
    getCellBounds(endX, endY, bx1, by1, bx2, by2);
    for (int y = by1 + wallThickness; y < by2 - wallThickness; y++) {
        for (int x = bx2 - wallThickness; x < gridWidth_; x++) {
             if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_)
                blocked_[getIndex(x, y)] = false;
        }
    }
    
    mazeExitX_ = gridWidth_ - goalWidth / 2;
    mazeExitY_ = (by1 + by2) / 2;
    
    std::cout << "[TRUE MAZE] Level " << complexityLevel 
              << ": " << mazeCellsX << "x" << mazeCellsY 
              << " = " << mazeCellCount_ << " cells" << std::endl;
    
    // convert the blocked_ array into a list of rectangles for drawing.
    // run length encode each row: contiguous blocked spans become 1 pixel tall obstacles
    for (int y = 0; y < gridHeight_; y++) {
        int runStart = -1;  // starting at X of current blocked run or -1 if none
        for (int x = 0; x <= gridWidth_; x++) {
            bool isBlocked = (x < gridWidth_) && blocked_[getIndex(x, y)];
            if (isBlocked && runStart < 0) {
                runStart = x;  // starting a new blocked run.
            } else if (!isBlocked && runStart >= 0) {
                // ending a blocked run; emit an obstacle rectangle
                obstacles_.push_back({runStart, y, x - runStart, 1});
                runStart = -1;
            }
        }
    }
}

void Pathfinder::carvePassage(int x1, int y1, int x2, int y2, int width) {
    // carves a straight corridor from (x1,y1) to (x2,y2) with the given width
    // and determines which direction to step in (−1, 0, or +1 per axis)
    int dx = (x2 > x1) ? 1 : (x2 < x1) ? -1 : 0;
    int dy = (y2 > y1) ? 1 : (y2 < y1) ? -1 : 0;
    
    int x = x1, y = y1;
    while (x != x2 || y != y2) {
        // clears a width × width square centered on current (x, y)
        for (int wy = -width/2; wy <= width/2; wy++) {
            for (int wx = -width/2; wx <= width/2; wx++) {
                int cx = x + wx, cy = y + wy;
                if (cx >= 0 && cx < gridWidth_ && cy >= 0 && cy < gridHeight_) {
                    blocked_[getIndex(cx, cy)] = false;
                }
            }
        }
        // and advance horizontally first then vertically (like an "L" shaped path)
        if (x != x2) x += dx;
        else if (y != y2) y += dy;
    }
}

void Pathfinder::generateLabyrinthMaze(int spawnMargin, int goalMargin, float difficulty) {
    clearObstacles();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int barrierLeft = spawnMargin + 1;
    int barrierRight = gridWidth_ - goalMargin - 1;
    int topWallThickness = 3;
    int topGapStart = gridHeight_ / 3;
    int topGapEnd = gridHeight_ * 2 / 3;
    
    //CONTAINMENT WALLS
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = 0; y < topWallThickness; y++) {
            blocked_[getIndex(x, y)] = true;
        }
        for (int y = gridHeight_ - topWallThickness; y < gridHeight_; y++) {
            blocked_[getIndex(x, y)] = true;
        }
    }
    obstacles_.push_back({barrierLeft, 0, barrierRight - barrierLeft, topWallThickness});
    obstacles_.push_back({barrierLeft, gridHeight_ - topWallThickness, barrierRight - barrierLeft, topWallThickness});
    
    for (int y = 0; y < topGapStart; y++) {
        blocked_[getIndex(barrierLeft, y)] = true;
        blocked_[getIndex(barrierRight, y)] = true;
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        blocked_[getIndex(barrierLeft, y)] = true;
        blocked_[getIndex(barrierRight, y)] = true;
    }
    obstacles_.push_back({barrierLeft, 0, 1, topGapStart});
    obstacles_.push_back({barrierLeft, topGapEnd, 1, gridHeight_ - topGapEnd});
    obstacles_.push_back({barrierRight, 0, 1, topGapStart});
    obstacles_.push_back({barrierRight, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    int mazeTop = topWallThickness + 2;
    int mazeBottom = gridHeight_ - topWallThickness - 2;
    int mazeLeft = barrierLeft;
    int mazeRight = barrierRight;
    int mazeWidth = mazeRight - mazeLeft;
    int mazeHeight = mazeBottom - mazeTop;
    
    // creates a grid based labyrinth with walls
    // wall grid - walls on even indices passages on odd
    int cellSize = 6;  // each cell is 6 grid units
    int wallThickness = 2;
    
    int cellsX = mazeWidth / cellSize;
    int cellsY = mazeHeight / cellSize;
    
    // track which walls exist (start with all walls)
    // horizontal walls: [cellsX][cellsY+1]
    // vertical walls: [cellsX+1][cellsY]
    std::vector<std::vector<bool>> hWalls(cellsX, std::vector<bool>(cellsY + 1, true));
    std::vector<std::vector<bool>> vWalls(cellsX + 1, std::vector<bool>(cellsY, true));
    
    // using recursive backtracking to remove walls and create maze
    std::vector<std::vector<bool>> visited(cellsX, std::vector<bool>(cellsY, false));
    std::stack<std::pair<int, int>> stack;
    
    // starting from left-middle
    int startX = 0;
    int startY = cellsY / 2;
    stack.push({startX, startY});
    visited[startX][startY] = true;
    
    int dx[] = {1, 0, -1, 0};  // right, down, left, up
    int dy[] = {0, 1, 0, -1};
    
    // recursive backtracker to carve a perfect maze
    while (!stack.empty()) {
        auto [cx, cy] = stack.top();  // current cell
        
        // find all unvisited neighbors
        std::vector<int> unvisited;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            if (nx >= 0 && nx < cellsX && ny >= 0 && ny < cellsY && !visited[nx][ny]) {
                unvisited.push_back(d);
            }
        }
        
        if (unvisited.empty()) {
            stack.pop();  // dead end: backtrack
        } else {
            // pick a random unvisited neighbor and remove the wall between
            int dir = unvisited[gen() % unvisited.size()];
            int nx = cx + dx[dir];
            int ny = cy + dy[dir];
            
            // remove wall between current and neighbor
            // vertical walls block horizontal movement and horizontal walls block vertical.
            if (dir == 0) vWalls[cx + 1][cy] = false;      // right
            else if (dir == 1) hWalls[cx][cy + 1] = false; // down
            else if (dir == 2) vWalls[cx][cy] = false;     // left
            else if (dir == 3) hWalls[cx][cy] = false;     // up
            
            visited[nx][ny] = true;
            stack.push({nx, ny});
        }
    }
    
    // removal of entrance and exit walls
    vWalls[0][cellsY / 2] = false;
    vWalls[cellsX][cellsY / 2] = false;
    
    // extra openings based on difficulty (more openings = more potential (sub)optimal paths)
    int extraOpenings = static_cast<int>((1.0f - difficulty) * cellsX * cellsY * 0.2f);
    for (int i = 0; i < extraOpenings; i++) {
        if (gen() % 2 == 0 && cellsX > 1) {
            int wx = 1 + (gen() % (cellsX - 1));
            int wy = gen() % cellsY;
            vWalls[wx][wy] = false;
        } else if (cellsY > 1) {
            int wx = gen() % cellsX;
            int wy = 1 + (gen() % (cellsY - 1));
            hWalls[wx][wy] = false;
        }
    }
    
    // now draws the walls that remain
    // and each horizontal will wall span one cell width and sits between rows
    for (int cx = 0; cx < cellsX; cx++) {
        for (int wy = 0; wy <= cellsY; wy++) {
            if (hWalls[cx][wy]) {
                // compute world space rectangle for this wall segment
                int wallX = mazeLeft + cx * cellSize;
                int wallY = mazeTop + wy * cellSize - wallThickness / 2;
                int wallW = cellSize;
                
                // and mark every pixel in the wall as blocked
                for (int y = wallY; y < wallY + wallThickness; y++) {
                    for (int x = wallX; x < wallX + wallW; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({wallX, wallY, wallW, wallThickness});
            }
        }
    }
    
    // each vertical wall spans one cell height and sits between columns
    for (int wx = 0; wx <= cellsX; wx++) {
        for (int cy = 0; cy < cellsY; cy++) {
            if (vWalls[wx][cy]) {
                int wallX = mazeLeft + wx * cellSize - wallThickness / 2;
                int wallY = mazeTop + cy * cellSize;
                int wallH = cellSize;
                
                for (int y = wallY; y < wallY + wallH; y++) {
                    for (int x = wallX; x < wallX + wallThickness; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({wallX, wallY, wallThickness, wallH});
            }
        }
    }
}

void Pathfinder::generateMultiPathMaze(int spawnMargin, int goalMargin, float difficulty) {
    clearObstacles();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int barrierLeft = spawnMargin + 1;
    int barrierRight = gridWidth_ - goalMargin - 1;
    int topWallThickness = 3;
    int topGapStart = gridHeight_ / 3;
    int topGapEnd = gridHeight_ * 2 / 3;
    
    //CONTAINMENT WALLS (like original)
    // top wall - blocks top bypass
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = 0; y < topWallThickness; y++) {
            if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                blocked_[getIndex(x, y)] = true;
            }
        }
    }
    obstacles_.push_back({barrierLeft, 0, barrierRight - barrierLeft, topWallThickness});
    
    // bottom wall - blocks bottom bypass
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = gridHeight_ - topWallThickness; y < gridHeight_; y++) {
            if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                blocked_[getIndex(x, y)] = true;
            }
        }
    }
    obstacles_.push_back({barrierLeft, gridHeight_ - topWallThickness, barrierRight - barrierLeft, topWallThickness});
    
    // left funnel wall - leaves gap in middle
    for (int y = 0; y < topGapStart; y++) {
        if (barrierLeft >= 0 && barrierLeft < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierLeft, y)] = true;
        }
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        if (barrierLeft >= 0 && barrierLeft < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierLeft, y)] = true;
        }
    }
    obstacles_.push_back({barrierLeft, 0, 1, topGapStart});
    obstacles_.push_back({barrierLeft, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    // right funnel wall - leaves gap in middle
    for (int y = 0; y < topGapStart; y++) {
        if (barrierRight >= 0 && barrierRight < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierRight, y)] = true;
        }
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        if (barrierRight >= 0 && barrierRight < gridWidth_ && y >= 0 && y < gridHeight_) {
            blocked_[getIndex(barrierRight, y)] = true;
        }
    }
    obstacles_.push_back({barrierRight, 0, 1, topGapStart});
    obstacles_.push_back({barrierRight, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    //internal multi path barriers
    int mazeTop = topWallThickness + 2;
    int mazeBottom = gridHeight_ - topWallThickness - 2;
    int wallThickness = 2;
    
    // creating horizontal barriers with multiple gaps (forcing path choices)
    int numBarriers = 6 + static_cast<int>(difficulty * 4);  // more barriers than original
    int spacing = (mazeBottom - mazeTop - 10) / (numBarriers + 1);
    
    for (int i = 1; i <= numBarriers; i++) {
        int barrierY = mazeTop + 5 + i * spacing;
        
        // creating 3-5 gaps per barrier giving agents multiple path choices meaning more suboptimal routes
        // for more naive pathfinding attempts to get stuck on...
        int numGaps = 3 + (gen() % 3);  // 3-5 gaps
        std::vector<std::pair<int, int>> gaps;
        
        // distribute gaps roughly evenly across the barrier.
        for (int g = 0; g < numGaps; g++) {
            int gapWidth = 6 + (gen() % 5);  // 6-10 cells wide
            int section = (barrierRight - barrierLeft) / numGaps;
            int gapStart = barrierLeft + g * section + (gen() % std::max(1, section - gapWidth));
            gaps.push_back({gapStart, gapStart + gapWidth});
        }
        
        // drawing barrier, skipping any positions that fall inside a gap
        int thickness = 1 + (gen() % 2);
        for (int t = 0; t < thickness; t++) {
            int y = barrierY + t;
            if (y >= gridHeight_ - 2) continue;
            
            for (int x = barrierLeft; x < barrierRight; x++) {
                bool inGap = false;
                for (auto& gap : gaps) {
                    if (x >= gap.first && x < gap.second) {
                        inGap = true;
                        break;
                    }
                }
                if (inGap) continue;  // leave this column open for agents, agents like gaps
                
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        
        // record wall segments (the runs between gaps) for rendering
        int segStart = barrierLeft;
        for (auto& gap : gaps) {
            if (gap.first > segStart) {
                obstacles_.push_back({segStart, barrierY, gap.first - segStart, thickness});
            }
            segStart = gap.second;  // jump past the gap.
        }
        // final segment after the last gap
        if (barrierRight > segStart) {
            obstacles_.push_back({segStart, barrierY, barrierRight - segStart, thickness});
        }
    }
    
    // vertical obstacles to create branching paths
    // these chop horizontal paths into more varied routes.
    int numVertWalls = 8 + static_cast<int>(difficulty * 6);
    for (int v = 0; v < numVertWalls; v++) {
        int wallX = barrierLeft + 10 + (gen() % (barrierRight - barrierLeft - 20));
        int wallH = 15 + (gen() % 25);  // height of this vertical segment
        int wallY = mazeTop + (gen() % (mazeBottom - mazeTop - wallH));
        
        // marking the rectangle as blocked
        for (int y = wallY; y < wallY + wallH; y++) {
            for (int x = wallX; x < wallX + wallThickness; x++) {
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        obstacles_.push_back({wallX, wallY, wallThickness, wallH});
    }
}

void Pathfinder::generateBottleneckMaze(int spawnMargin, int goalMargin, float difficulty) {
    clearObstacles();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int barrierLeft = spawnMargin + 1;
    int barrierRight = gridWidth_ - goalMargin - 1;
    int topWallThickness = 3;
    int topGapStart = gridHeight_ / 3;
    int topGapEnd = gridHeight_ * 2 / 3;
    
    //containment walls
    // top wall
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = 0; y < topWallThickness; y++) {
            if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                blocked_[getIndex(x, y)] = true;
            }
        }
    }
    obstacles_.push_back({barrierLeft, 0, barrierRight - barrierLeft, topWallThickness});
    
    // bottom wall
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = gridHeight_ - topWallThickness; y < gridHeight_; y++) {
            if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                blocked_[getIndex(x, y)] = true;
            }
        }
    }
    obstacles_.push_back({barrierLeft, gridHeight_ - topWallThickness, barrierRight - barrierLeft, topWallThickness});
    
    // left funnel
    for (int y = 0; y < topGapStart; y++) {
        if (barrierLeft >= 0 && barrierLeft < gridWidth_) blocked_[getIndex(barrierLeft, y)] = true;
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        if (barrierLeft >= 0 && barrierLeft < gridWidth_) blocked_[getIndex(barrierLeft, y)] = true;
    }
    obstacles_.push_back({barrierLeft, 0, 1, topGapStart});
    obstacles_.push_back({barrierLeft, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    // Right funnel
    for (int y = 0; y < topGapStart; y++) {
        if (barrierRight >= 0 && barrierRight < gridWidth_) blocked_[getIndex(barrierRight, y)] = true;
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        if (barrierRight >= 0 && barrierRight < gridWidth_) blocked_[getIndex(barrierRight, y)] = true;
    }
    obstacles_.push_back({barrierRight, 0, 1, topGapStart});
    obstacles_.push_back({barrierRight, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    int mazeTop = topWallThickness + 2;
    int mazeBottom = gridHeight_ - topWallThickness - 2;
    int mazeLeft = barrierLeft;
    int mazeRight = barrierRight;
    int mazeWidth = mazeRight - mazeLeft;
    
    // bottleneck walls - vertical walls with gaps
    int numBottlenecks = 3 + static_cast<int>(difficulty * 3);  // 3-6 bottlenecks
    int spacing = (barrierRight - barrierLeft) / (numBottlenecks + 1);
    
    for (int bn = 0; bn < numBottlenecks; bn++) {
        int wallX = barrierLeft + (bn + 1) * spacing;
        int wallThickness = 3;
        
        // number of gaps in this wall (fewer = harder)
        int numGaps = 3 - static_cast<int>(difficulty * 2);  // 1-3 gaps
        if (numGaps < 1) numGaps = 1;
        
        // gap positions
        std::vector<std::pair<int, int>> gaps;  // y start, y end
        int gapSpacing = (mazeBottom - mazeTop) / (numGaps + 1);
        int gapSize = 10 - static_cast<int>(difficulty * 5);  // 5-10 cells
        if (gapSize < 4) gapSize = 4;
        
        for (int g = 0; g < numGaps; g++) {
            int gapCenter = mazeTop + (g + 1) * gapSpacing;
            gaps.push_back({gapCenter - gapSize/2, gapCenter + gapSize/2});
        }
        
        // draw wall segments between gaps
        int segStart = mazeTop;
        for (auto& gap : gaps) {
            if (gap.first - segStart > 2) {
                // wall segment
                for (int y = segStart; y < gap.first; y++) {
                    for (int x = wallX; x < wallX + wallThickness; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({wallX, segStart, wallThickness, gap.first - segStart});
            }
            segStart = gap.second;
        }
        // final segment
        if (mazeBottom - segStart > 2) {
            for (int y = segStart; y < mazeBottom; y++) {
                for (int x = wallX; x < wallX + wallThickness; x++) {
                    if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                        blocked_[getIndex(x, y)] = true;
                    }
                }
            }
            obstacles_.push_back({wallX, segStart, wallThickness, mazeBottom - segStart});
        }
    }
    
    // some horizontal walls between bottlenecks for extra challenge
    int numHorizontal = 2 + static_cast<int>(difficulty * 4);
    for (int h = 0; h < numHorizontal; h++) {
        int hx = mazeLeft + 10 + (gen() % (mazeWidth - 20));
        int hy = mazeTop + 5 + (gen() % (mazeBottom - mazeTop - 10));
        int hw = 15 + (gen() % 25);
        int hh = 2;
        
        // a check so it doesnt extend too far
        if (hx + hw > mazeRight - 5) hw = mazeRight - 5 - hx;
        
        for (int y = hy; y < hy + hh; y++) {
            for (int x = hx; x < hx + hw; x++) {
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        if (hw > 5) obstacles_.push_back({hx, hy, hw, hh});
    }
    
    // scattered blocks to create more obstacles
    int numBlocks = 8 + static_cast<int>(difficulty * 8);
    for (int b = 0; b < numBlocks; b++) {
        int bx = mazeLeft + 5 + (gen() % (mazeWidth - 10));
        int by = mazeTop + 3 + (gen() % (mazeBottom - mazeTop - 6));
        int bw = 2 + (gen() % 4);
        int bh = 2 + (gen() % 4);
        
        for (int y = by; y < by + bh; y++) {
            for (int x = bx; x < bx + bw; x++) {
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        obstacles_.push_back({bx, by, bw, bh});
    }
}
// refactor
// doesnt really work as intended...
void Pathfinder::generateSpiralMaze(int spawnMargin, int goalMargin, float difficulty) {
    clearObstacles();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int barrierLeft = spawnMargin + 1;
    int barrierRight = gridWidth_ - goalMargin - 1;
    int topWallThickness = 3;
    int topGapStart = gridHeight_ / 3;
    int topGapEnd = gridHeight_ * 2 / 3;
    
    // containment walls
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = 0; y < topWallThickness; y++) {
            blocked_[getIndex(x, y)] = true;
        }
        for (int y = gridHeight_ - topWallThickness; y < gridHeight_; y++) {
            blocked_[getIndex(x, y)] = true;
        }
    }
    obstacles_.push_back({barrierLeft, 0, barrierRight - barrierLeft, topWallThickness});
    obstacles_.push_back({barrierLeft, gridHeight_ - topWallThickness, barrierRight - barrierLeft, topWallThickness});
    
    for (int y = 0; y < topGapStart; y++) {
        blocked_[getIndex(barrierLeft, y)] = true;
        blocked_[getIndex(barrierRight, y)] = true;
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        blocked_[getIndex(barrierLeft, y)] = true;
        blocked_[getIndex(barrierRight, y)] = true;
    }
    obstacles_.push_back({barrierLeft, 0, 1, topGapStart});
    obstacles_.push_back({barrierLeft, topGapEnd, 1, gridHeight_ - topGapEnd});
    obstacles_.push_back({barrierRight, 0, 1, topGapStart});
    obstacles_.push_back({barrierRight, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    int mazeTop = topWallThickness + 2;
    int mazeBottom = gridHeight_ - topWallThickness - 2;
    int mazeWidth = barrierRight - barrierLeft;
    int mazeHeight = mazeBottom - mazeTop;
    int mazeLeft = barrierLeft;
    
    int cx = mazeLeft + mazeWidth / 2;
    int cy = mazeTop + mazeHeight / 2;
    
    // create spiral walls instead of carving through solid
    float maxRadius = std::min(mazeWidth, mazeHeight) / 2.0f - 5;
    int wallThickness = 2;
    int numArms = 2 + static_cast<int>(difficulty * 2);  // 2-4 spiral arms
    
    // each arm is a separate spiral emanating from center
    for (int arm = 0; arm < numArms; arm++) {
        // evenly distribute arm starting angles around the circle
        float startAngle = arm * (2.0f * 3.14159f / numArms);
        float angle = startAngle;
        float radius = 8;  // start a bit away from center
        
        // each arm has one gap (passage) at a random angle
        float gapAngle = startAngle + 3.14159f * (0.3f + (gen() % 100) / 200.0f);
        
        // waling outward in a spiral drawing wall segments
        while (radius < maxRadius) {
            // and skip drawing if we're near the gap angle (so to leave a passage)
            float angleDiff = std::fmod(std::abs(angle - gapAngle), 2.0f * 3.14159f);
            if (angleDiff > 0.3f || radius < 15) {
                // convert polar (radius, angle) to cartesian (px, py)
                int px = cx + static_cast<int>(radius * std::cos(angle));
                int py = cy + static_cast<int>(radius * std::sin(angle));
                
                // draw a small wall segment (wallThickness * wallThickness).
                for (int dy = 0; dy < wallThickness; dy++) {
                    for (int dx = 0; dx < wallThickness; dx++) {
                        int tx = px + dx, ty = py + dy;
                        if (tx >= mazeLeft && tx < mazeLeft + mazeWidth && ty >= mazeTop && ty < mazeBottom) {
                            blocked_[getIndex(tx, ty)] = true;
                        }
                    }
                }
            }
            
            // advance angle and radius to trace a spiral
            angle += 0.08f;
            radius += 0.12f;
        }
    }
    
    // obstacle list construction from blocked cells by grouping adjacent blocked pixels
    // into small rectangles for rendering. scanning in 4x4 chunks
    for (int y = mazeTop; y < mazeBottom; y += 4) {
        for (int x = mazeLeft; x < mazeLeft + mazeWidth; x += 4) {
            if (blocked_[getIndex(x, y)]) {
                // expands rightward and downward to find extent of this blocked region.
                int w = 1, h = 1;
                while (x + w < mazeLeft + mazeWidth && w < 4 && blocked_[getIndex(x + w, y)]) w++;
                while (y + h < mazeBottom && h < 4 && blocked_[getIndex(x, y + h)]) h++;
                obstacles_.push_back({x, y, w, h});
            }
        }
    }
    
    // radial walls (straight spokes from center) for extra complexity
    int numRadials = 3 + static_cast<int>(difficulty * 3);
    for (int r = 0; r < numRadials; r++) {
        // random angle for this spoke.
        float rAngle = (gen() % 360) * 3.14159f / 180.0f;
        float rStart = 12 + (gen() % 15);  // start radius.
        float rEnd = rStart + 15 + (gen() % 20);  // end radius.
        
        // walk outward along this angle drawing wall pixels
        for (float rad = rStart; rad < rEnd && rad < maxRadius; rad += 1.0f) {
            int px = cx + static_cast<int>(rad * std::cos(rAngle));
            int py = cy + static_cast<int>(rad * std::sin(rAngle));
            
            // draws a 2x2 block at this position
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    if (px + dx >= mazeLeft && px + dx < mazeLeft + mazeWidth && 
                        py + dy >= mazeTop && py + dy < mazeBottom) {
                        blocked_[getIndex(px + dx, py + dy)] = true;
                    }
                }
            }
        }
        // record an approximate obstacle rectangle for rendering.
        obstacles_.push_back({cx + static_cast<int>(rStart * std::cos(rAngle)),
                             cy + static_cast<int>(rStart * std::sin(rAngle)),
                             2, static_cast<int>(rEnd - rStart)});
    }
}

void Pathfinder::generateChambersMaze(int spawnMargin, int goalMargin, float difficulty) {
    clearObstacles();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int barrierLeft = spawnMargin + 1;
    int barrierRight = gridWidth_ - goalMargin - 1;
    int topWallThickness = 3;
    int topGapStart = gridHeight_ / 3;
    int topGapEnd = gridHeight_ * 2 / 3;
    
    for (int x = barrierLeft; x < barrierRight; x++) {
        for (int y = 0; y < topWallThickness; y++) {
            blocked_[getIndex(x, y)] = true;
        }
        for (int y = gridHeight_ - topWallThickness; y < gridHeight_; y++) {
            blocked_[getIndex(x, y)] = true;
        }
    }
    obstacles_.push_back({barrierLeft, 0, barrierRight - barrierLeft, topWallThickness});
    obstacles_.push_back({barrierLeft, gridHeight_ - topWallThickness, barrierRight - barrierLeft, topWallThickness});
    
    for (int y = 0; y < topGapStart; y++) {
        blocked_[getIndex(barrierLeft, y)] = true;
        blocked_[getIndex(barrierRight, y)] = true;
    }
    for (int y = topGapEnd; y < gridHeight_; y++) {
        blocked_[getIndex(barrierLeft, y)] = true;
        blocked_[getIndex(barrierRight, y)] = true;
    }
    obstacles_.push_back({barrierLeft, 0, 1, topGapStart});
    obstacles_.push_back({barrierLeft, topGapEnd, 1, gridHeight_ - topGapEnd});
    obstacles_.push_back({barrierRight, 0, 1, topGapStart});
    obstacles_.push_back({barrierRight, topGapEnd, 1, gridHeight_ - topGapEnd});
    
    int mazeTop = topWallThickness + 2;
    int mazeBottom = gridHeight_ - topWallThickness - 2;
    int mazeWidth = barrierRight - barrierLeft;
    int mazeHeight = mazeBottom - mazeTop;
    int mazeLeft = barrierLeft;
    
    // chambers by building walls around them (not fill then carve)
    int numChamberCols = 3 + static_cast<int>(difficulty * 2);  // 3-5 columns
    int numChamberRows = 2 + static_cast<int>(difficulty);  // 2-3 rows
    
    int chamberW = mazeWidth / numChamberCols;
    int chamberH = mazeHeight / numChamberRows;
    int wallThickness = 2;
    
    // draw horizontal walls between rows of chambers
    // each wall has gaps so agents can pass between rows
    for (int row = 1; row < numChamberRows; row++) {
        int wallY = mazeTop + row * chamberH;
        
        // for each column create a wall segment with a gap
        for (int col = 0; col < numChamberCols; col++) {
            int segLeft = mazeLeft + col * chamberW;
            int segRight = segLeft + chamberW;
            
            // these random gap positions are within this column's segment
            int gapX = segLeft + chamberW / 4 + (gen() % (chamberW / 2));
            int gapWidth = 6 + (gen() % 6);
            
            // draw left segment (before the gap)
            if (gapX - segLeft > 3) {
                for (int y = wallY; y < wallY + wallThickness; y++) {
                    for (int x = segLeft; x < gapX; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({segLeft, wallY, gapX - segLeft, wallThickness});
            }
            
            // draw right segment (after the gap)
            if (segRight - (gapX + gapWidth) > 3) {
                for (int y = wallY; y < wallY + wallThickness; y++) {
                    for (int x = gapX + gapWidth; x < segRight; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({gapX + gapWidth, wallY, segRight - (gapX + gapWidth), wallThickness});
            }
        }
    }
    
    // drawing vertical walls between columns of chambers
    // each wall has gaps so agents can pass between columns
    for (int col = 1; col < numChamberCols; col++) {
        int wallX = mazeLeft + col * chamberW;
        
        for (int row = 0; row < numChamberRows; row++) {
            int segTop = mazeTop + row * chamberH;
            int segBottom = segTop + chamberH;
            
            int gapY = segTop + chamberH / 4 + (gen() % (chamberH / 2));
            int gapHeight = 6 + (gen() % 6);
            
            if (gapY - segTop > 3) {
                for (int y = segTop; y < gapY; y++) {
                    for (int x = wallX; x < wallX + wallThickness; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({wallX, segTop, wallThickness, gapY - segTop});
            }
            
            if (segBottom - (gapY + gapHeight) > 3) {
                for (int y = gapY + gapHeight; y < segBottom; y++) {
                    for (int x = wallX; x < wallX + wallThickness; x++) {
                        if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                            blocked_[getIndex(x, y)] = true;
                        }
                    }
                }
                obstacles_.push_back({wallX, gapY + gapHeight, wallThickness, segBottom - (gapY + gapHeight)});
            }
        }
    }
    
    // scatter small random obstacles inside chambers for extra difficulty?
    int numInternalObstacles = static_cast<int>(difficulty * 15);
    for (int i = 0; i < numInternalObstacles; i++) {
        // random position and size
        int ox = mazeLeft + 5 + (gen() % (mazeWidth - 10));
        int oy = mazeTop + 5 + (gen() % (mazeHeight - 10));
        int ow = 3 + (gen() % 5);
        int oh = 3 + (gen() % 5);
        
        // mark rectangle as blocked
        for (int y = oy; y < oy + oh; y++) {
            for (int x = ox; x < ox + ow; x++) {
                if (x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_) {
                    blocked_[getIndex(x, y)] = true;
                }
            }
        }
        obstacles_.push_back({ox, oy, ow, oh});
    }
}

void Pathfinder::floodFillCheck(int startX, int startY, int goalX, int goalY) {
    // simple BFS to verify a path exists (just used for debugging)
    // doesn't return the path - just confirms reachability.
    std::queue<std::pair<int, int>> q;
    std::vector<bool> visited(gridWidth_ * gridHeight_, false);
    
    q.push({startX, startY});
    visited[getIndex(startX, startY)] = true;
    
    // cardinal directions only.
    int dx[] = {0, 1, 0, -1};
    int dy[] = {1, 0, -1, 0};
    
    // standard BFS: process queue until empty or goal found.
    while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop();
        
        if (x == goalX && y == goalY) return;  // path exists - done.
        
        // queue unvisited passable neighbors
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d];
            int ny = y + dy[d];
            if (nx >= 0 && nx < gridWidth_ && ny >= 0 && ny < gridHeight_ &&
                !visited[getIndex(nx, ny)] && !blocked_[getIndex(nx, ny)]) {
                visited[getIndex(nx, ny)] = true;
                q.push({nx, ny});
            }
        }
    }
    // if we exit the loop, no path was found. again this is just a debug helper
}

bool Pathfinder::isBlocked(int gridX, int gridY) const {
    if (gridX < 0 || gridX >= gridWidth_ || gridY < 0 || gridY >= gridHeight_) {
        return true;
    }
    return blocked_[getIndex(gridX, gridY)];
}

bool Pathfinder::isBlocked(const GridCell& cell) const {
    return isBlocked(cell.x, cell.y);
}

bool Pathfinder::isValid(int x, int y) const {
    return x >= 0 && x < gridWidth_ && y >= 0 && y < gridHeight_ && !blocked_[getIndex(x, y)];
}

bool Pathfinder::isValid(const GridCell& cell) const {
    return isValid(cell.x, cell.y);
}

GridCell Pathfinder::worldToGrid(float worldX, float worldY) const {
    return {static_cast<int>(worldX / cellSize_), static_cast<int>(worldY / cellSize_)};
}

std::pair<float, float> Pathfinder::gridToWorld(const GridCell& cell) const {
    return gridToWorld(cell.x, cell.y);
}

std::pair<float, float> Pathfinder::gridToWorld(int gridX, int gridY) const {
    return {(gridX + 0.5f) * cellSize_, (gridY + 0.5f) * cellSize_};
}

float Pathfinder::heuristic(const GridCell& a, const GridCell& b) const {
    // octile distance (good for octal directional movement)
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    return static_cast<float>(std::max(dx, dy) + 0.414f * std::min(dx, dy));
}

float Pathfinder::euclideanDistance(const GridCell& a, const GridCell& b) const {
    float dx = static_cast<float>(a.x - b.x);
    float dy = static_cast<float>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

float Pathfinder::manhattanDistance(const GridCell& a, const GridCell& b) const {
    return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

std::vector<GridCell> Pathfinder::getNeighbors(const GridCell& cell, bool allowDiagonal) const {
    std::vector<GridCell> neighbors;
    neighbors.reserve(8);  // max 8 directions
    
    // direction offsets: first 4 are cardinal (N, E, S, W). and next 4 are diagonals
    const int dx[] = {0, 1, 0, -1, 1, 1, -1, -1};
    const int dy[] = {-1, 0, 1, 0, -1, 1, 1, -1};
    int count = allowDiagonal ? 8 : 4;  // limit to cardinals if diagonals disallowed
    
    for (int i = 0; i < count; i++) {
        int nx = cell.x + dx[i];
        int ny = cell.y + dy[i];
        
        if (isValid(nx, ny)) {
            // for diagonal moves (indices 4-7), we want to make sure we dont cut through a blocked corner
            // becasue both adjacent cardinal cells must be passable.
            if (i >= 4) {
                bool canPassX = isValid(cell.x + dx[i], cell.y);  // horizontal step
                bool canPassY = isValid(cell.x, cell.y + dy[i]);  // vertical step
                if (!canPassX || !canPassY) continue;  // skip this diagonal
            }
            neighbors.push_back({nx, ny});
        }
    }
    
    return neighbors;
}

std::vector<GridCell> Pathfinder::reconstructPath(
    const std::unordered_map<GridCell, GridCell, GridCellHash>& cameFrom,
    const GridCell& start, const GridCell& goal) {
    
    std::vector<GridCell> path;
    GridCell current = goal;
    
    // walking backwards through the parent map from goal toward start
    while (current != start) {
        path.push_back(current);
        auto it = cameFrom.find(current);
        if (it == cameFrom.end()) break;  // this shouldnt happen if path is valid
        current = it->second;             // move to parent cell.
    }
    path.push_back(start);
    
    // building goal->start so flip it to start->goal
    std::reverse(path.begin(), path.end());
    return path;
}

// simplify path by removing unnecessary waypoints using line of sight checks
// this produces a smoother path with fewer turns.
std::vector<GridCell> Pathfinder::simplifyPath(const std::vector<GridCell>& path) const {
    if (path.size() <= 2) return path;  // nothing to simplify.
    
    std::vector<GridCell> simplified;
    simplified.push_back(path[0]);  // always keep start.
    
    size_t current = 0;
    while (current < path.size() - 1) {
        // just finds the furthest point visible from current via line of sight
        // we search backward from the end so we skip as many points as possible
        size_t furthest = current + 1;
        for (size_t i = path.size() - 1; i > current + 1; i--) {
            if (lineOfSight(path[current], path[i])) {
                furthest = i;
                break;
            }
        }
        simplified.push_back(path[furthest]);
        current = furthest;  // jump ahead
    }
    
    return simplified;
}

float Pathfinder::calculatePathLength(const std::vector<GridCell>& path) const {
    float length = 0.0f;
    for (size_t i = 1; i < path.size(); i++) {
        length += euclideanDistance(path[i-1], path[i]);
    }
    return length;
}

bool Pathfinder::lineOfSight(const GridCell& a, const GridCell& b) const {
    // Bresenham's line algorithm to check line of sight.
    // steps along the integer grid from a to b and returns false the moment
    // we hit any blocked cell
    int x0 = a.x, y0 = a.y;
    int x1 = b.x, y1 = b.y;
    
    int dx = std::abs(x1 - x0);  // horizontal span.
    int dy = std::abs(y1 - y0);  // vertical span.
    int sx = (x0 < x1) ? 1 : -1; // step direction on x.
    int sy = (y0 < y1) ? 1 : -1; // step direction on y.
    int err = dx - dy;           // error accumulator (Bresenham).
    
    while (true) {
        if (isBlocked(x0, y0)) return false;  // hit a wall so no line of sight.
        
        if (x0 == x1 && y0 == y1) break;  // reached destination.
        
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }  // step horizontally.
        if (e2 < dx)  { err += dx; y0 += sy; }  // step vertically.
    }
    
    return true;
}


// A* algorithm
PathResult Pathfinder::findPathAStar(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PathResult result;
    result.nodesExpanded = 0;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    if (start == goal) {
        result.found = true;
        result.path = {start};
        return result;
    }
    
    // priority queue holds (f score, cell) pairs and smallest f first.
    using PQElement = std::pair<float, GridCell>;
    std::priority_queue<PQElement, std::vector<PQElement>, std::greater<PQElement>> frontier;
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFrom;   // parent pointers.
    std::unordered_map<GridCell, float, GridCellHash> costSoFar;     // g score per cell.
    
    frontier.push({0.0f, start});
    costSoFar[start] = 0.0f;
    
    // main A* loop: pop lowest f score cell expand neighbors
    while (!frontier.empty()) {
        GridCell current = frontier.top().second;
        frontier.pop();
        result.nodesExpanded++;
        
        if (current == goal) {
            // reconstructs path by walking parent pointers back to start
            result.found = true;
            result.path = reconstructPath(cameFrom, start, goal);
            result.pathLength = calculatePathLength(result.path);
            break;
        }
        
        // expanding each neighbor
        for (const GridCell& neighbor : getNeighbors(current)) {
            // the edge cost is true euclidean distance so diagonals are slightly more expensive.
            float moveCost = euclideanDistance(current, neighbor);
            float newCost = costSoFar[current] + moveCost;  // tentative g...
            
            // if this path to neighbor is better than any previous then we record it.
            if (costSoFar.find(neighbor) == costSoFar.end() || newCost < costSoFar[neighbor]) {
                costSoFar[neighbor] = newCost;
                float priority = newCost + heuristic(neighbor, goal);  // f = g + h
                frontier.push({priority, neighbor});
                cameFrom[neighbor] = current;
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}


// Greedy best first search
PathResult Pathfinder::findPathGreedy(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PathResult result;
    result.nodesExpanded = 0;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    if (start == goal) {
        result.found = true;
        result.path = {start};
        return result;
    }
    
    // greedy uses only h (heuristic) as priority which ignores path cost (g)
    using PQElement = std::pair<float, GridCell>;
    std::priority_queue<PQElement, std::vector<PQElement>, std::greater<PQElement>> frontier;
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFrom;
    std::unordered_set<GridCell, GridCellHash> visited;
    
    frontier.push({heuristic(start, goal), start});
    
    // popping cell closest to goal (by heuristic) expand neighbors
    while (!frontier.empty()) {
        GridCell current = frontier.top().second;
        frontier.pop();
        
        if (visited.find(current) != visited.end()) continue;  // already processed
        visited.insert(current);
        result.nodesExpanded++;
        
        if (current == goal) {
            result.found = true;
            result.path = reconstructPath(cameFrom, start, goal);
            result.pathLength = calculatePathLength(result.path);
            break;
        }
        
        for (const GridCell& neighbor : getNeighbors(current)) {
            if (visited.find(neighbor) == visited.end()) {
                float priority = heuristic(neighbor, goal);  // only h, no g
                frontier.push({priority, neighbor});
                // record parent pointer (first discovery wins)
                if (cameFrom.find(neighbor) == cameFrom.end()) {
                    cameFrom[neighbor] = current;
                }
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}


// Bidirectional BFS - UNINFORMED SEARCH
// not currently using

PathResult Pathfinder::findPathBidirectional(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PathResult result;
    result.nodesExpanded = 0;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    if (start == goal) {
        result.found = true;
        result.path = {start};
        return result;
    }
    
    std::queue<GridCell> frontierForward, frontierBackward;
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFromForward, cameFromBackward;
    std::unordered_set<GridCell, GridCellHash> visitedForward, visitedBackward;
    
    frontierForward.push(start);
    frontierBackward.push(goal);
    visitedForward.insert(start);
    visitedBackward.insert(goal);
    cameFromForward[start] = start;  // sentinel for path reconstruction.
    cameFromBackward[goal] = goal;
    
    GridCell meetingPoint = {-1, -1};  // where the two searches meet.
    
    // alternate expanding forward and backward until they intersect.
    
    while (!frontierForward.empty() && !frontierBackward.empty()) {
        // expand forward BFS by one layer.
        if (!frontierForward.empty()) {
            GridCell current = frontierForward.front();
            frontierForward.pop();
            result.nodesExpanded++;
            
            // checking if backward search already visited this cell (meeting point)
            if (visitedBackward.find(current) != visitedBackward.end()) {
                meetingPoint = current;
                break;
            }
            
            // queue up unvisited neighbors
            for (const GridCell& neighbor : getNeighbors(current)) {
                if (visitedForward.find(neighbor) == visitedForward.end()) {
                    visitedForward.insert(neighbor);
                    cameFromForward[neighbor] = current;
                    frontierForward.push(neighbor);
                }
            }
        }
        
        // expand backward BFS by one layer
        if (!frontierBackward.empty()) {
            GridCell current = frontierBackward.front();
            frontierBackward.pop();
            result.nodesExpanded++;
            
            // and check if forward search already visited this cell
            if (visitedForward.find(current) != visitedForward.end()) {
                meetingPoint = current;
                break;
            }
            
            for (const GridCell& neighbor : getNeighbors(current)) {
                if (visitedBackward.find(neighbor) == visitedBackward.end()) {
                    visitedBackward.insert(neighbor);
                    cameFromBackward[neighbor] = current;
                    frontierBackward.push(neighbor);
                }
            }
        }
    }
    
    // reconstruction of paths by stitching forward and backward halves
    if (meetingPoint.x != -1) {
        result.found = true;
        
        // walk backward from meeting point to start
        std::vector<GridCell> pathForward;
        GridCell current = meetingPoint;
        while (current != start) {
            pathForward.push_back(current);
            current = cameFromForward[current];
        }
        pathForward.push_back(start);
        std::reverse(pathForward.begin(), pathForward.end());  // now start->meeting.
        
        // walk forward from meeting point to goal using backward tree
        current = cameFromBackward[meetingPoint];
        while (current != goal) {
            pathForward.push_back(current);
            current = cameFromBackward[current];
        }
        pathForward.push_back(goal);
        
        result.path = pathForward;
        result.pathLength = calculatePathLength(result.path);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}


// Jump point search (JPS)

GridCell Pathfinder::jump(const GridCell& current, int dx, int dy, const GridCell& goal) {
    // attempt one step in direction (dx, dy).
    GridCell next = {current.x + dx, current.y + dy};
    
    if (!isValid(next)) {
        return {-1, -1};  // hit obstacle or out of bounds.
    }
    
    if (next == goal) {
        return next;  // found the goal directly.
    }
    
    // JPS prunes redundant nodes by checking for "forced neighbors" which are
    // cells that become reachable only through this particular direction.
    if (dx != 0 && dy != 0) {
        // diagonal movement: forced neighbor exists if we can cut around an obstacle corner.
        if ((isValid(current.x - dx, current.y + dy) && !isValid(current.x - dx, current.y)) ||
            (isValid(current.x + dx, current.y - dy) && !isValid(current.x, current.y - dy))) {
            return next;  // this is a jump point
        }
        
        // recurse into horizontal and vertical arms from this diagonal step.
        if (jump(next, dx, 0, goal).x != -1 || jump(next, 0, dy, goal).x != -1) {
            return next;  // found something interesting down one arm.
        }
    } else {
        // straight (cardinal) movement: check perpendicular walls that create forced neighbors
        if (dx != 0) {
            // moving horizontally - check above and below
            if ((isValid(next.x, next.y + 1) && !isValid(current.x, current.y + 1)) ||
                (isValid(next.x, next.y - 1) && !isValid(current.x, current.y - 1))) {
                return next;
            }
        } else {
            // or moving vertically - check left and right
            if ((isValid(next.x + 1, next.y) && !isValid(current.x + 1, current.y)) ||
                (isValid(next.x - 1, next.y) && !isValid(current.x - 1, current.y))) {
                return next;
            }
        }
    }
    
    // nothing special here so keep jumping in the same direction.
    return jump(next, dx, dy, goal);
}

PathResult Pathfinder::findPathJPS(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PathResult result;
    result.nodesExpanded = 0;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    if (start == goal) {
        result.found = true;
        result.path = {start};
        return result;
    }
    
    // JPS uses same data structures as A* but prunes many intermediate nodes
    using PQElement = std::pair<float, GridCell>;
    std::priority_queue<PQElement, std::vector<PQElement>, std::greater<PQElement>> frontier;
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFrom;  // parent of each jump point
    std::unordered_map<GridCell, float, GridCellHash> costSoFar;    // g score
    
    frontier.push({0.0f, start});
    costSoFar[start] = 0.0f;
    cameFrom[start] = start;
    
    // 8 directions: cardinals + diagonals
    const int dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
    const int dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};
    
    // JPS loop: expand jump points rather than every neighbor
    while (!frontier.empty()) {
        GridCell current = frontier.top().second;
        frontier.pop();
        result.nodesExpanded++;
        
        if (current == goal) {
            result.found = true;
            result.path = reconstructPath(cameFrom, start, goal);
            result.pathLength = calculatePathLength(result.path);
            break;
        }
        
        // jump in all 8 directions skipping any that hit walls immediately.
        for (int i = 0; i < 8; i++) {
            GridCell jumpPoint = jump(current, dx[i], dy[i], goal);
            
            if (jumpPoint.x == -1) continue;  // no jump point in this direction
            
            float moveCost = euclideanDistance(current, jumpPoint);
            float newCost = costSoFar[current] + moveCost;
            
            if (costSoFar.find(jumpPoint) == costSoFar.end() || newCost < costSoFar[jumpPoint]) {
                costSoFar[jumpPoint] = newCost;
                float priority = newCost + heuristic(jumpPoint, goal);
                frontier.push({priority, jumpPoint});
                cameFrom[jumpPoint] = current;
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}


// theta* (any angle)

PathResult Pathfinder::findPathTheta(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PathResult result;
    result.nodesExpanded = 0;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    if (start == goal) {
        result.found = true;
        result.path = {start};
        return result;
    }
    
    using PQElement = std::pair<float, GridCell>;
    std::priority_queue<PQElement, std::vector<PQElement>, std::greater<PQElement>> frontier;
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFrom;  // parent pointer (any angle).
    std::unordered_map<GridCell, float, GridCellHash> gScore;
    std::unordered_set<GridCell, GridCellHash> closedSet;
    
    frontier.push({heuristic(start, goal), start});
    gScore[start] = 0.0f;
    cameFrom[start] = start;
    
    // theta* main loop like A* but here it tries to bypass intermediate nodes
    while (!frontier.empty()) {
        GridCell current = frontier.top().second;
        frontier.pop();
        
        if (closedSet.find(current) != closedSet.end()) continue;  // already finalized.
        closedSet.insert(current);
        result.nodesExpanded++;
        
        if (current == goal) {
            result.found = true;
            result.path = reconstructPath(cameFrom, start, goal);
            result.pathLength = calculatePathLength(result.path);
            break;
        }
        
        for (const GridCell& neighbor : getNeighbors(current)) {
            if (closedSet.find(neighbor) != closedSet.end()) continue;
            
            GridCell parent = cameFrom[current];
            float newG;
            GridCell newParent;
            
            // theta* key insight is if theres a direct line of sight from grandparent
            // skip current entirely and connect parent->neighbor.
            if (lineOfSight(parent, neighbor)) {
                newG = gScore[parent] + euclideanDistance(parent, neighbor);
                newParent = parent;  // shortcut
            } else {
                newG = gScore[current] + euclideanDistance(current, neighbor);
                newParent = current; // normal A* style parent
            }
            
            if (gScore.find(neighbor) == gScore.end() || newG < gScore[neighbor]) {
                gScore[neighbor] = newG;
                cameFrom[neighbor] = newParent;
                float f = newG + heuristic(neighbor, goal);
                frontier.push({f, neighbor});
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}

PathResult Pathfinder::findPathDFS(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    PathResult result;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    // dfs uses a stack (LIFO) so it dives deep before backtracking.
    std::stack<GridCell> frontier;
    frontier.push(start);
    
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFrom;
    cameFrom[start] = start;
    
    std::unordered_set<GridCell, GridCellHash> visited;
    visited.insert(start);
    
    // random number generator for randomized DFS: shuffling neighbor order
    // creates chaotic branching paths (like a lightning bolt or burning wool sort of effect)
    std::random_device rd;
    std::mt19937 g(rd());
    
    while (!frontier.empty()) {
        GridCell current = frontier.top();
        frontier.pop();
        result.nodesExpanded++;
        
        if (current == goal) {
            result.found = true;
            result.path = reconstructPath(cameFrom, start, goal);
            result.pathLength = calculatePathLength(result.path);
            break;
        }
        
        std::vector<GridCell> neighbors = getNeighbors(current);
        
        // shuffle neighbors so the search picks a random direction each time
        // without shuffle dfs would always prefer one fixed direction.
        std::shuffle(neighbors.begin(), neighbors.end(), g);
        
        // pushes unvisited neighbors onto the stack.
        for (const auto& next : neighbors) {
            if (visited.find(next) == visited.end()) {
                visited.insert(next);
                cameFrom[next] = current;  // record parent
                frontier.push(next);
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}


// OG Dijkstras algorithm - a uniform cost search with true Euclidean costs
// unlike BFS (which treats all moves as cost 1) dijkstra uses actual distances:
// cardinal moves (N,S,E,W) cost 1.0
// diagonal moves cost sqrt(2) ≈ 1.414
// this creates an octagonal wavefront instead of a perfect circle

// currently not working as intended

PathResult Pathfinder::findPathDijkstra(const GridCell& start, const GridCell& goal) {
    auto startTime = std::chrono::high_resolution_clock::now();
    PathResult result;
    
    if (!isValid(start) || !isValid(goal)) {
        return result;
    }
    
    // cost constants
    const float CARDINAL_COST = 1.0f;
    const float DIAGONAL_COST = 1.41421356f;  // sqrt(2)
    
    // priority queue: (cost, cell) - min heap by cost
    using PQEntry = std::pair<float, GridCell>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> frontier;
    
    // cost to reach each cell (g score in A* terminology)
    std::unordered_map<GridCell, float, GridCellHash> costSoFar;
    
    // parent tracking for path reconstruction
    std::unordered_map<GridCell, GridCell, GridCellHash> cameFrom;
    
    frontier.push({0.0f, start});
    costSoFar[start] = 0.0f;
    cameFrom[start] = start;
    
    // dijkstra loop: always expand the lowest cost node first.
    while (!frontier.empty()) {
        auto [currentCost, current] = frontier.top();
        frontier.pop();
        result.nodesExpanded++;
        
        // skip if we've already found a better path to this node.
        if (costSoFar.count(current) && currentCost > costSoFar[current]) {
            continue;
        }
        
        if (current == goal) {
            result.found = true;
            result.path = reconstructPath(cameFrom, start, goal);
            result.pathLength = calculatePathLength(result.path);
            break;
        }
        
        // and expand all 8 directional neighbors
        std::vector<GridCell> neighbors = getNeighbors(current, true);
        
        for (const auto& next : neighbors) {
            // edge compute cost: sqrt(2) for diagonal and 1.0 for cardinal
            int dx = std::abs(next.x - current.x);
            int dy = std::abs(next.y - current.y);
            bool isDiagonal = (dx == 1 && dy == 1);
            
            float moveCost = isDiagonal ? DIAGONAL_COST : CARDINAL_COST;
            float newCost = costSoFar[current] + moveCost;
            
            // a check if we havent visited this cell or found a cheaper path
            if (!costSoFar.count(next) || newCost < costSoFar[next]) {
                costSoFar[next] = newCost;
                cameFrom[next] = current;
                frontier.push({newCost, next});
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.computeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    return result;
}


// generic dispatcher
PathResult Pathfinder::findPath(SimulationSettings::Algos algo, const GridCell& start, const GridCell& goal) {
    PathResult result;
    
    switch (algo) {
        case SimulationSettings::Algos::Slime:
            // slime agents use native sense/move behavior instead of pre computed paths
            result.found = true;  // marks as "found" so agent is created
            result.path = {};     // empty path = use native slime behavior
            break;
        case SimulationSettings::Algos::DFS:
            result = findPathDFS(start, goal);
            break;
        case SimulationSettings::Algos::Dijkstra:
            result = findPathDijkstra(start, goal);
            break;
            
        // === PATHFINDERS (goal-aware) ===
        case SimulationSettings::Algos::AStar:
            result = findPathAStar(start, goal);
            break;
        case SimulationSettings::Algos::Greedy:
            result = findPathGreedy(start, goal);
            break;
        case SimulationSettings::Algos::JPS:
            result = findPathJPS(start, goal);
            break;
        case SimulationSettings::Algos::Theta:
            result = findPathTheta(start, goal);
            break;
        default:
            // fallback to AStar
            result = findPathAStar(start, goal);
            break;
    }
    
    return result;
}

PathResult Pathfinder::findPath(SimulationSettings::Algos algo, float startX, float startY, float goalX, float goalY) {
    GridCell start = worldToGrid(startX, startY);
    GridCell goal = worldToGrid(goalX, goalY);
    return findPath(algo, start, goal);
}

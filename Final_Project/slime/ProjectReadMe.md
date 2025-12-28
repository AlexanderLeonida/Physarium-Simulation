# Slime/Ants Maze Solving Algorithms Final Project

## Explicit technical documentation

Reference [here](../slime/documentation/) for technical documentation
And [here](../slime/documentation/ProjectReadme.md) for a second, more technical user manual for building the project

## Presentation slide deck
https://docs.google.com/presentation/d/1etBA5dLRFejuCIcTu_SxVt7SVCNyJ8OT-f3pRm80gtw/edit?usp=sharing

## Overview

The goal of this project is to compare and contrast different algorithms that we learned during our time over this past semester with improved versions of those algorithms that we found online. In this specific project, we are comparing the differences between six different path finding algorithms through a maze. Then, by using the doubling method, we can compare the empirical time complexities of these algorithms and compare those time complexities against the theoretical worst case time complexities in order to determine how well our simulation truly represented these different algorithms. By the end of this simulation, we will also be able to determine which algorithms perform the best under specific maze scenarios that they're put in. 

## How to run the program

This project is located in the 'slime' folder. From the root repository directory, navigate to Final_Project/slime. Here, there should be a predefined tasks.json file that you can run. Because this project uses SFML for it's graphics, you might need to install that as well to populate some of the necessary header files. If there are errors in the terminal when you attempt to run the tasks.json file located under the .vscode directory, let us know and we can fix them for you if needed. 

The core of our project runs from Pathfinder.cpp, but the simulation being initially ran from main.cpp traces to the PhysarumSimulation.cpp file which then traces to the Pathfinder.cpp file. 

When you build and then run the program, you'll find yourself looking at Nic's previous slime project. You need to click the F button in order to remove the original large block of text that appears on your GUI. Then, click shift + D to to toggle off and on the shaders for the project. When you click M on your keyboard, you'll notice the scene of the project beginning to change. Continuously click the M button until you reach our maze path finding simulation. 

When you reach the maze path finding simulation, you will be able to click the space bar in order to run the program. If you click the space bar again, the simulation should reset. Similarly, if you click the N button on your keyboard, you will be able to toggle through the diffferent maze settings that we have in varying levels of complexity. If you want to start all of the different path finding algorithms in the same position, use the P button on your keyboard. The D button also recalculates the time complexities of the algorithms based on the doubling method. If you want to toggle on and off specific algorithms in order to visually isolate specific colors, you can use the shift button on your keyboard in combination with any number between 1 and 7 in order to toggle on and off specific algorithms for the simulation. 

## Assumptions

In this project we assume that the user knows how to set up projects. We assume that they know how to install libraries, export them to path, etc. We assume that the user knows how run a project with a task file. In this case, if your IDE of choice is vscode, you can simply use command + shift + b. We also assume that this user has a device capable of handling the computational stress of this program. This project is compatible with Windows and Mac, but I'm sure that linux should also not be too much of a problem. 

## Algorithmic pseudocode 

Reference [here](../slime/documentation/algos.md) for algorithm list, further assumptions, and time complexity analysis. 

1. **Dijkstra’s Algorithm** – the classic non heuristic finds the shortest path on weighted graphs. Slow but reliable.

```
BEGIN DIJKSTRA(start, goal)

    IF start or goal is invalid THEN
        RETURN no path

    Initialize a priority queue of frontier nodes
    Insert the start node with cost 0

    Set the cost of the start node to 0
    Mark the start node as having no parent

    WHILE the frontier is not empty DO

        Remove the node with the smallest cost; call it CURRENT

        IF CURRENT is the goal THEN
            Mark the goal as reached
            EXIT the loop
        ENDIF

        FOR each neighbor of CURRENT DO

            Determine the movement cost to reach the neighbor

            Compute the new total cost to the neighbor

            IF the neighbor has never been visited
               OR the new total cost is smaller than the previously known cost
            THEN
                Update the neighbor’s cost
                Record that the neighbor’s parent is CURRENT
                Insert the neighbor into the frontier
            ENDIF

        END FOR

    END WHILE

    IF the goal was reached THEN
        Reconstruct the path by following parent links backward
        RETURN the reconstructed path
    ELSE
        RETURN no path
    ENDIF

END DIJKSTRA
```

2. **A\*** – The superstar. That uses heuristics (like euclidean or manhattan distance) to speed up dijkstra.

```
BEGIN A_STAR(start, goal)

    IF start or goal invalid THEN RETURN no path
    IF start = goal THEN RETURN [start]

    Add start to frontier with priority 0
    Set cost-to-reach(start) = 0

    WHILE frontier not empty DO

        CURRENT = node in frontier with lowest priority

        IF CURRENT = goal THEN EXIT loop

        FOR each neighbor of CURRENT DO

            Compute movement cost to neighbor
            Compute new cost-to-reach

            IF neighbor unvisited OR new cost is lower THEN
                Update cost-to-reach(neighbor)
                Compute priority = cost-to-reach + heuristic to goal
                Add neighbor to frontier with priority
                Set parent(neighbor) = CURRENT
            ENDIF

        END FOR

    END WHILE

    IF goal reached THEN
        RETURN path reconstructed from parents
    ELSE
        RETURN no path
    ENDIF

END A_STAR
```

3. **Depth First Search (DFS)** – Not efficient for shortest path, but it technically counts as pathfinding in like an exploration context I guess.

```
BEGIN DFS(start, goal)

    IF start or goal invalid THEN RETURN no path

    Push start onto stack
    Mark start as visited
    Set parent(start) = start

    WHILE stack not empty DO

        CURRENT = pop from stack

        IF CURRENT = goal THEN EXIT loop

        Get neighbors of CURRENT
        Randomly shuffle neighbor order

        FOR each neighbor DO
            IF neighbor not visited THEN
                Mark neighbor visited
                Set parent(neighbor) = CURRENT
                Push neighbor onto stack
            ENDIF
        END FOR

    END WHILE

    IF goal reached THEN
        RETURN reconstructed path
    ELSE
        RETURN no path
    ENDIF

END DFS
```

4. **Greedy Best First Search** – Like A* but only cares about the heuristic, not the cost so far. Often pretty stupid but fast.

```
BEGIN GREEDY(start, goal)

    IF start or goal invalid THEN RETURN no path
    IF start = goal THEN RETURN [start]

    Add start to frontier with priority = heuristic(start, goal)
    Mark no nodes visited yet

    WHILE frontier not empty DO

        CURRENT = node in frontier with lowest heuristic

        IF CURRENT already visited THEN CONTINUE
        Mark CURRENT visited

        IF CURRENT = goal THEN EXIT loop

        FOR each neighbor of CURRENT DO
            IF neighbor not visited THEN
                Add neighbor to frontier with priority = heuristic(neighbor, goal)
                IF neighbor has no parent THEN parent(neighbor) = CURRENT
            ENDIF
        END FOR

    END WHILE

    IF goal reached THEN
        RETURN reconstructed path
    ELSE
        RETURN no path
    ENDIF

END GREEDY
```

5. **Bidirectional Search** – Runs search forward from start and backward from goal hoping they meet in the middle. This is still in progress on Ethan's branch.

6. **Jump Point Search (JPS)** – Optimization of A* for uniform grids cuts down on redundant nodes.

```
FUNCTION JUMP(current, direction, goal)

    Step from CURRENT one cell in given direction → NEXT
    IF NEXT is invalid THEN RETURN no jump point
    IF NEXT = goal THEN RETURN NEXT

    IF moving diagonally THEN
        IF forced neighbor exists around NEXT THEN
            RETURN NEXT
        ENDIF

        IF JUMP(NEXT, horizontal part of direction, goal) found OR
           JUMP(NEXT, vertical part of direction, goal) found
        THEN
            RETURN NEXT
        ENDIF

    ELSE  // moving straight
        IF forced neighbor exists perpendicular to direction THEN
            RETURN NEXT
        ENDIF
    ENDIF

    // continue jumping forward
    RETURN JUMP(NEXT, direction, goal)

END FUNCTION

BEGIN JPS(start, goal)

    IF start or goal invalid THEN RETURN no path
    IF start = goal THEN RETURN [start]

    Add start to frontier with priority 0
    Set cost-to-reach(start) = 0
    Set parent(start) = start

    WHILE frontier not empty DO

        CURRENT = node with lowest priority
        IF CURRENT = goal THEN EXIT loop

        FOR each of 8 directions DO
            JUMP_POINT = JUMP(CURRENT, direction, goal)

            IF no jump point found THEN CONTINUE

            Compute movement cost from CURRENT to JUMP_POINT
            Compute new cost-to-reach

            IF JUMP_POINT unvisited OR cost improves THEN
                Update cost-to-reach(JUMP_POINT)
                Compute priority = cost + heuristic to goal
                Add JUMP_POINT to frontier
                Set parent(JUMP_POINT) = CURRENT
            ENDIF
        END FOR

    END WHILE

    IF goal reached THEN
        RETURN reconstructed path
    ELSE
        RETURN no path
    ENDIF

END JPS
```

7. **D\* Lite** – Dynamic A*. Originally designed for robotics where the map changes as you explore. Maybe drop this one? This has not yet been implemented

8.  **Theta\*** – Variant of A* that allows any angle paths instead of restricting to grid connections.

```
START TIMER
INIT result

IF start OR goal invalid THEN RETURN result
IF start == goal THEN RETURN trivial path

INIT frontier WITH start (priority = h(start))
SET g(start) = 0
SET parent(start) = start
INIT closedSet

WHILE frontier NOT empty DO
    current = frontier.pop()
    IF current IN closedSet THEN CONTINUE
    ADD current TO closedSet
    INCREMENT nodesExpanded

    IF current == goal THEN
        BUILD path FROM parent pointers
        STOP search
    ENDIF

    FOR EACH neighbor OF current DO
        IF neighbor IN closedSet THEN CONTINUE

        grandparent = parent(current)

        IF lineOfSight(grandparent, neighbor) THEN
            newCost = g(grandparent) + dist(grandparent, neighbor)
            newParent = grandparent
        ELSE
            newCost = g(current) + dist(current, neighbor)
            newParent = current
        ENDIF

        IF neighbor NOT SEEN OR newCost < g(neighbor) THEN
            UPDATE g(neighbor)
            SET parent(neighbor) = newParent
            PUSH neighbor WITH priority = g(neighbor) + h(neighbor)
        ENDIF
    END FOR
END WHILE

STOP TIMER; RECORD time
RETURN result
```

9. **Floyd–Warshall** – All pairs shortest paths. Less about “find one path now” and more about “know every possible path always.” This has not yet been implemented. 

## Complexity analysis 

Detailed analysis of emperical vs worst case theoretical run times [here](../slime/documentation/algos.md)

## Citations

Citations listed throughout the entire project. Citations listed in code, documentation folder, and in references folder for the slime project which this maze project was slightly based off of. 
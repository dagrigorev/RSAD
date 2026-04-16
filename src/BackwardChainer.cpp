// =============================================================================
// BackwardChainer.cpp — Реализация обратной трассировки M^{-1}
// =============================================================================
#include "BackwardChainer.h"
#include <queue>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>
#include <cstdio>
#include <numeric>

BackwardChainer::BackwardChainer(const WorldModel& world) : world_(world) {}

// ---------------------------------------------------------------------------
// M^{-1} для GridWorld
//
//  Если из (px,py) применить действие a, то попадём в (x,y).
//  Значит: px = x - DX[a], py = y - DY[a]
//
//  DX: UP=0, DOWN=0, LEFT=-1, RIGHT=+1
//  DY: UP=-1, DOWN=+1, LEFT=0, RIGHT=0
// ---------------------------------------------------------------------------
std::pair<int,int> BackwardChainer::inverseApply(int x, int y, Action a) const {
    // Если из предшественника применить a → (x,y), то предшественник = (x - dx, y - dy)
    switch(a) {
        case Action::UP:    return {x,  y+1};  // UP двигает вверх (-y), значит пришли снизу
        case Action::DOWN:  return {x,  y-1};  // DOWN двигает вниз (+y), пришли сверху
        case Action::LEFT:  return {x+1,y  };  // LEFT двигает влево (-x), пришли справа
        case Action::RIGHT: return {x-1,y  };  // RIGHT двигает вправо (+x), пришли слева
        default:            return {x,  y  };
    }
}

std::pair<int,int> BackwardChainer::forwardApply(int x, int y, Action a) const {
    switch(a) {
        case Action::UP:    return {x,  y-1};
        case Action::DOWN:  return {x,  y+1};
        case Action::LEFT:  return {x-1,y  };
        case Action::RIGHT: return {x+1,y  };
        default:            return {x,  y  };
    }
}

// ---------------------------------------------------------------------------
// Вспомогательные
// ---------------------------------------------------------------------------
float BackwardChainer::obstacleDesity(const Problem& p, int cx, int cy, int r) const {
    int total = 0, obs = 0;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int nx = cx+dx, ny = cy+dy;
            if (nx<0||nx>=p.gridSize||ny<0||ny>=p.gridSize) continue;
            total++;
            if (p.isObstacle(nx,ny)) obs++;
        }
    return total>0 ? (float)obs/total : 0.f;
}

std::pair<int,int> BackwardChainer::clampFree(const Problem& p, int x, int y) const {
    x = std::max(0, std::min(p.gridSize-1, x));
    y = std::max(0, std::min(p.gridSize-1, y));
    if (!p.isObstacle(x,y)) return {x,y};
    // Ищем ближайшую свободную клетку по спирали
    for (int r = 1; r <= p.gridSize; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx)!=r && abs(dy)!=r) continue;
                int nx=x+dx, ny=y+dy;
                if (nx<0||nx>=p.gridSize||ny<0||ny>=p.gridSize) continue;
                if (!p.isObstacle(nx,ny)) return {nx,ny};
            }
    return {p.startX, p.startY};
}

// ---------------------------------------------------------------------------
// Ключевой метод: BFS через обратный граф M^{-1}
//
//  Строим путь от toX,toY НАЗАД до fromX,fromY:
//    - В каждом узле (x,y) генерируем M^{-1}(x,y,a) для всех a
//    - Если предшественник свободен — добавляем в очередь
//    - Восстанавливаем путь: помним (пришли из, какое прямое действие дало этот узел)
//
//  Результат: последовательность ПРЯМЫХ действий от (fromX,fromY) до (toX,toY)
// ---------------------------------------------------------------------------
std::vector<Action> BackwardChainer::solveSegment(
    int fromX, int fromY, int toX, int toY, const Problem& p) const
{
    if (fromX==toX && fromY==toY) return {};
    if (p.isObstacle(toX,toY) || p.isObstacle(fromX,fromY)) return {};

    int G = p.gridSize, GG = G*G;
    std::vector<int>    dist(GG, -1);
    // parent[flat] = {parent_flat, action_that_leads_from_parent_to_flat}
    std::vector<int>    parentFlat(GG, -1);
    std::vector<Action> parentAct (GG, Action::NONE);

    int startFlat = toY*G + toX;   // Начинаем обратный BFS с ЦЕЛИ
    int endFlat   = fromY*G + fromX; // Завершаем на СТАРТЕ

    dist[startFlat] = 0;
    std::queue<int> q;
    q.push(startFlat);

    // Обратный BFS: из (x,y) пробуем все a, вычисляем M^{-1}(x,y,a)
    while (!q.empty()) {
        int flat = q.front(); q.pop();
        int cx = flat%G, cy = flat/G;

        if (flat == endFlat) break;

        for (int ai = 0; ai < 4; ai++) {
            Action a = static_cast<Action>(ai);
            // Кто мог прийти в (cx,cy) при действии a?
            auto [px, py] = inverseApply(cx, cy, a);
            if (px<0||px>=G||py<0||py>=G) continue;
            if (p.isObstacle(px,py)) continue;

            int pFlat = py*G + px;
            if (dist[pFlat] != -1) continue;

            dist[pFlat]       = dist[flat] + 1;
            parentFlat[pFlat] = flat;
            parentAct [pFlat] = a;  // Прямое действие: из pFlat применить a → flat
            q.push(pFlat);
        }
    }

    if (dist[endFlat] == -1) return {};  // Нет пути

    // Восстановление: идём от endFlat (=fromX,fromY) к startFlat (=toX,toY)
    // по цепочке parentFlat, собирая parentAct — это прямые действия
    std::vector<Action> path;
    int cur = endFlat;
    while (cur != startFlat) {
        path.push_back(parentAct[cur]);
        cur = parentFlat[cur];
        if (cur < 0) { path.clear(); break; }
    }

    // path собран в порядке from→to (каждый parentAct — прямое действие)
    return path;
}

// ---------------------------------------------------------------------------
// Сборка BackwardTrace по набору waypoints
// ---------------------------------------------------------------------------
BackwardTrace BackwardChainer::solveWithWaypoints(
    const Problem& p,
    const std::vector<std::pair<int,int>>& wps,
    WaypointStrategy strategy) const
{
    BackwardTrace trace;
    trace.strategy  = strategy;
    trace.waypoints = wps;

    // Добавляем start и goal как первую и последнюю точки
    std::vector<std::pair<int,int>> all;
    all.push_back({p.startX, p.startY});
    for (auto& wp : wps) all.push_back(wp);
    all.push_back({p.goalX, p.goalY});

    Solution full;
    bool allOk = true;

    for (int i = 0; i+1 < (int)all.size(); i++) {
        PathSegment seg;
        seg.fromX = all[i].first;   seg.fromY = all[i].second;
        seg.toX   = all[i+1].first; seg.toY   = all[i+1].second;

        // Решаем отрезок через M^{-1}
        seg.actions   = solveSegment(seg.fromX, seg.fromY, seg.toX, seg.toY, p);
        seg.reachable = !seg.actions.empty();

        if (!seg.reachable) {
            allOk = false;
            // Пробуем обойти через ближайшую свободную точку
            auto [fx, fy] = clampFree(p, (seg.fromX+seg.toX)/2, (seg.fromY+seg.toY)/2);
            auto seg1 = solveSegment(seg.fromX, seg.fromY, fx, fy, p);
            auto seg2 = solveSegment(fx, fy, seg.toX, seg.toY, p);
            if (!seg1.empty() && !seg2.empty()) {
                seg.actions.insert(seg.actions.end(), seg1.begin(), seg1.end());
                seg.actions.insert(seg.actions.end(), seg2.begin(), seg2.end());
                seg.reachable = true;
                allOk = true;
            }
        }

        full.actions.insert(full.actions.end(), seg.actions.begin(), seg.actions.end());
        trace.segments.push_back(seg);

        if (!seg.reachable) { allOk = false; break; }
    }

    trace.complete = allOk;
    trace.solution = full;

    RSAD_LOG("[BackwardChainer] strategy=%d waypoints=%zu complete=%s len=%zu\n",
             (int)strategy, wps.size(),
             allOk ? "YES" : "NO", full.actions.size());
    return trace;
}

// ---------------------------------------------------------------------------
// Стратегия 1: DIAGONAL — через угловые точки четырёх квадрантов
// ---------------------------------------------------------------------------
BackwardTrace BackwardChainer::traceDiagonal(const Problem& p) const {
    int G = p.gridSize;
    int q1x = G/4, q1y = G/4;
    int q2x = 3*G/4, q2y = 3*G/4;

    std::vector<std::pair<int,int>> wps = {
        clampFree(p, q1x, q1y),
        clampFree(p, q2x, q2y)
    };
    return solveWithWaypoints(p, wps, WaypointStrategy::DIAGONAL);
}

// ---------------------------------------------------------------------------
// Стратегия 2: PERIMETER — обход вдоль края
// ---------------------------------------------------------------------------
BackwardTrace BackwardChainer::tracePerimeter(const Problem& p) const {
    int G = p.gridSize;
    // Идём: start → верхний правый угол → нижний правый → goal
    int margin = 1;
    std::vector<std::pair<int,int>> wps = {
        clampFree(p, G-1-margin, margin),
        clampFree(p, G-1-margin, G-1-margin)
    };
    return solveWithWaypoints(p, wps, WaypointStrategy::PERIMETER);
}

// ---------------------------------------------------------------------------
// Стратегия 3: OBSTACLE_AWARE — ищет свободные "проходы"
// ---------------------------------------------------------------------------
BackwardTrace BackwardChainer::traceObstacleAware(const Problem& p) const {
    int G = p.gridSize;
    // Строим "карту плотности препятствий" и ищем минимально загруженные клетки
    // в полосах x=G/4, G/2, 3G/4

    std::vector<std::pair<int,int>> wps;
    for (int cx : {G/4, G/2, 3*G/4}) {
        // По вертикали ищем строку с минимальной плотностью в радиусе 2
        float bestDens = 1e9f;
        int   bestY   = G/2;
        for (int cy = 1; cy < G-1; cy++) {
            if (p.isObstacle(cx,cy)) continue;
            float d = obstacleDesity(p, cx, cy, 2);
            if (d < bestDens) { bestDens = d; bestY = cy; }
        }
        wps.push_back(clampFree(p, cx, bestY));
    }
    return solveWithWaypoints(p, wps, WaypointStrategy::OBSTACLE_AVOID);
}

// ---------------------------------------------------------------------------
// Стратегия 4: RANDOM_INTERIOR — случайные внутренние точки (с seed)
// ---------------------------------------------------------------------------
BackwardTrace BackwardChainer::traceRandomInterior(
    const Problem& p, unsigned seed) const
{
    std::mt19937 rng(seed);
    int G = p.gridSize;
    std::uniform_int_distribution<int> rx(1, G-2), ry(1, G-2);

    int numWP = 2 + (int)(seed % 3);  // 2..4 waypoints
    std::vector<std::pair<int,int>> wps;
    for (int i = 0; i < numWP; i++) {
        std::pair<int,int> wp = {rx(rng), ry(rng)};
        wp = clampFree(p, wp.first, wp.second);
        wps.push_back(wp);
    }
    // Сортируем по расстоянию от старта чтобы не было петель
    std::sort(wps.begin(), wps.end(), [&](auto& a, auto& b){
        float da = hypotf(a.first-p.startX, a.second-p.startY);
        float db = hypotf(b.first-p.startX, b.second-p.startY);
        return da < db;
    });

    return solveWithWaypoints(p, wps, WaypointStrategy::RANDOM_INTERIOR);
}

// ---------------------------------------------------------------------------
// Стратегия 5: QUADRANT_CENTERS — через центры квадрантов
// ---------------------------------------------------------------------------
BackwardTrace BackwardChainer::traceQuadrantCenters(const Problem& p) const {
    int G = p.gridSize;
    // Выбираем центры квадрантов, через которые нужно пройти
    // в зависимости от направления от start к goal
    bool goRight = (p.goalX > p.startX);
    bool goDown  = (p.goalY > p.startY);

    int midX = G/2, midY = G/2;

    std::vector<std::pair<int,int>> wps;
    if (goRight && goDown) {
        // Движение вправо-вниз: через (midX, startY) и (goalX, midY)
        wps.push_back(clampFree(p, midX, p.startY + (p.goalY-p.startY)/4));
        wps.push_back(clampFree(p, midX, midY));
    } else if (goRight && !goDown) {
        wps.push_back(clampFree(p, midX, midY));
    } else if (!goRight && goDown) {
        wps.push_back(clampFree(p, midX, midY));
    } else {
        // Движение влево-вверх: нестандартная декомпозиция
        wps.push_back(clampFree(p, G-G/4, G/4));
        wps.push_back(clampFree(p, G/4,   G/4));
    }

    return solveWithWaypoints(p, wps, WaypointStrategy::QUADRANT_CENTERS);
}

// ---------------------------------------------------------------------------
// Главный метод: generate()
// ---------------------------------------------------------------------------
std::vector<Solution> BackwardChainer::generate(
    const Problem& problem, int numVariants) const
{
    RSAD_LOG("[BackwardChainer] Generating %d solutions via M^{-1} decomposition\n",
             numVariants);

    std::vector<Solution> pool;
    std::vector<BackwardTrace> traces;

    // Запускаем все стратегии
    traces.push_back(traceDiagonal(problem));
    traces.push_back(tracePerimeter(problem));
    traces.push_back(traceObstacleAware(problem));
    traces.push_back(traceQuadrantCenters(problem));

    // Случайные варианты с разными seed
    for (int seed = 0; seed < numVariants - 4; seed++)
        traces.push_back(traceRandomInterior(problem, 777 + seed * 31));

    // Собираем успешные решения
    int success = 0;
    for (auto& trace : traces) {
        if (trace.complete && !trace.solution.actions.empty()) {
            pool.push_back(trace.solution);
            success++;
        } else if (!trace.solution.actions.empty()) {
            // Неполные решения тоже добавляем (могут быть ближе к цели)
            pool.push_back(trace.solution);
        }
    }

    RSAD_LOG("[BackwardChainer] %d/%d strategies succeeded, pool=%zu\n",
             success, (int)traces.size(), pool.size());
    return pool;
}

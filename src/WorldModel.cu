// =============================================================================
// WorldModel.cu — CUDA-реализация GridWorld + параллельный BFS
// =============================================================================
#include "WorldModel.h"
#include <cuda_runtime.h>
#include <thrust/device_vector.h>
#include <thrust/fill.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <queue>

// =============================================================================
// ============  CUDA ЯДРА  =====================================================
// =============================================================================

// -----------------------------------------------------------------------------
// Ядро 1: Пакетная симуляция решений
//   Каждый поток обрабатывает одно решение.
// -----------------------------------------------------------------------------
__global__ void simulateBatchKernel(
    const GPUSolution* __restrict__ solutions,
    const uint8_t*    __restrict__ grid,
    GPUSimResult*     results,
    int N,                // число решений
    int G,                // размер сетки
    int startX, int startY,
    int goalX,  int goalY)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N) return;

    const GPUSolution& sol = solutions[tid];
    GPUSimResult&      res = results[tid];

    int x = startX, y = startY;
    int steps = 0;
    bool hitObstacle  = false;
    bool reachedGoal  = false;

    for (int i = 0; i < sol.length && i < RSADConfig::MAX_SOL_LEN; ++i) {
        int nx = x, ny = y;
        switch (sol.actions[i]) {
            case 0: ny -= 1; break;  // UP
            case 1: ny += 1; break;  // DOWN
            case 2: nx -= 1; break;  // LEFT
            case 3: nx += 1; break;  // RIGHT
            default: break;
        }
        // Проверка границ
        if (nx < 0 || nx >= G || ny < 0 || ny >= G) {
            hitObstacle = true; break;
        }
        // Проверка препятствия
        if (grid[ny * G + nx]) {
            hitObstacle = true; break;
        }
        x = nx; y = ny; steps++;

        if (x == goalX && y == goalY) {
            reachedGoal = true; break;
        }
    }

    res.finalX       = x;
    res.finalY       = y;
    res.stepsUsed    = steps;
    res.reachedGoal  = reachedGoal ? 1 : 0;
    res.hitObstacle  = hitObstacle ? 1 : 0;
    float dx = (float)(x - goalX);
    float dy = (float)(y - goalY);
    res.distToGoal   = sqrtf(dx*dx + dy*dy);
}

// -----------------------------------------------------------------------------
// Ядро 2: BFS — расширение фронтира
//   Уровень BFS обрабатывается параллельно: каждый поток — одна ячейка фронтира
// -----------------------------------------------------------------------------
__global__ void bfsFrontierKernel(
    const int*   frontier,     // текущий фронтир (плоские индексы)
    int          frontierSize,
    const uint8_t* grid,       // карта препятствий
    int*         dist,         // расстояния от старта [G*G]
    int8_t*      parent,       // направление от родителя [G*G] (-1 = неизвестно)
    int*         nextFrontier, // новый фронтир (записывается)
    int*         nextFrontierSize, // атомарный счётчик нового фронтира
    int          G,
    int          currentDist)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= frontierSize) return;

    int flat = frontier[tid];
    int cx   = flat % G;
    int cy   = flat / G;

    // Попытка расширить в 4 направлениях
    // dx/dy для UP=0, DOWN=1, LEFT=2, RIGHT=3
    const int DX[4] = { 0,  0, -1,  1};
    const int DY[4] = {-1,  1,  0,  0};
    // "обратное" направление (для восстановления пути)
    // если мы пришли из UP (0), значит родитель находится DOWN (1)
    const int8_t BACK[4] = {1, 0, 3, 2};

    for (int d = 0; d < 4; ++d) {
        int nx = cx + DX[d];
        int ny = cy + DY[d];
        if (nx < 0 || nx >= G || ny < 0 || ny >= G) continue;
        if (grid[ny * G + nx]) continue;  // препятствие

        int nflat = ny * G + nx;
        // Атомарно записываем, если ещё не посещено
        int old = atomicCAS(&dist[nflat], INF_DIST, currentDist + 1);
        if (old == INF_DIST) {
            // Ячейка стала доступной — добавляем в следующий фронтир
            parent[nflat] = BACK[d];
            int pos = atomicAdd(nextFrontierSize, 1);
            nextFrontier[pos] = nflat;
        }
    }
}

// =============================================================================
// ============  РЕАЛИЗАЦИЯ WorldModel  ========================================
// =============================================================================

WorldModel::WorldModel(const Problem& problem)
    : problem_(problem), G_(problem.gridSize)
{
    uploadGrid();
}

WorldModel::~WorldModel() {
    if (d_grid_) { cudaFree(d_grid_); d_grid_ = nullptr; }
    freeBatchBuffers();
}

void WorldModel::uploadGrid() {
    size_t sz = (size_t)G_ * G_ * sizeof(uint8_t);
    CUDA_CHECK(cudaMalloc(&d_grid_, sz));

    std::vector<uint8_t> hostGrid(G_ * G_, 0);
    for (int i = 0; i < G_ * G_; ++i)
        hostGrid[i] = problem_.obstacles[i] ? 1 : 0;

    CUDA_CHECK(cudaMemcpy(d_grid_, hostGrid.data(), sz, cudaMemcpyHostToDevice));
}

void WorldModel::allocBatchBuffers(int n) const {
    if (n <= allocatedBatchSize_) return;
    freeBatchBuffers();
    CUDA_CHECK(cudaMalloc(&d_solutions_, n * sizeof(GPUSolution)));
    CUDA_CHECK(cudaMalloc(&d_results_,   n * sizeof(GPUSimResult)));
    allocatedBatchSize_ = n;
}

void WorldModel::freeBatchBuffers() const {
    if (d_solutions_) { cudaFree(d_solutions_); d_solutions_ = nullptr; }
    if (d_results_)   { cudaFree(d_results_);   d_results_   = nullptr; }
    allocatedBatchSize_ = 0;
}

// ---------------------------------------------------------------------------
// CPU симуляция (для отладки и единичных проверок)
// ---------------------------------------------------------------------------
SimResult WorldModel::simulateCPU(const Solution& sol) const {
    SimResult r;
    int x = problem_.startX, y = problem_.startY;

    for (auto a : sol.actions) {
        int nx = x, ny = y;
        switch (a) {
            case Action::UP:    ny--; break;
            case Action::DOWN:  ny++; break;
            case Action::LEFT:  nx--; break;
            case Action::RIGHT: nx++; break;
            default: break;
        }
        if (problem_.isObstacle(nx, ny)) {
            r.hitObstacle = true; break;
        }
        x = nx; y = ny; r.stepsUsed++;
        if (x == problem_.goalX && y == problem_.goalY) {
            r.reachedGoal = true; break;
        }
    }
    r.finalX = x; r.finalY = y;
    float dx = (float)(x - problem_.goalX);
    float dy = (float)(y - problem_.goalY);
    r.distToGoal = sqrtf(dx*dx + dy*dy);
    return r;
}

// ---------------------------------------------------------------------------
// GPU пакетная симуляция
// ---------------------------------------------------------------------------
std::vector<SimResult> WorldModel::simulateBatch(
    const std::vector<Solution>& solutions) const
{
    int N = static_cast<int>(solutions.size());
    if (N == 0) return {};

    allocBatchBuffers(N);

    // Упаковываем решения в GPU-структуры
    std::vector<GPUSolution> hostSols(N);
    for (int i = 0; i < N; ++i) {
        const auto& sol = solutions[i];
        int len = static_cast<int>(sol.actions.size());
        hostSols[i].length = std::min(len, RSADConfig::MAX_SOL_LEN);
        for (int j = 0; j < hostSols[i].length; ++j)
            hostSols[i].actions[j] = static_cast<uint8_t>(sol.actions[j]);
        for (int j = hostSols[i].length; j < RSADConfig::MAX_SOL_LEN; ++j)
            hostSols[i].actions[j] = static_cast<uint8_t>(Action::NONE);
    }

    CUDA_CHECK(cudaMemcpy(d_solutions_, hostSols.data(),
                          N * sizeof(GPUSolution), cudaMemcpyHostToDevice));

    int threads = RSADConfig::BATCH_THREADS;
    int blocks  = (N + threads - 1) / threads;

    simulateBatchKernel<<<blocks, threads>>>(
        d_solutions_, d_grid_, d_results_,
        N, G_,
        problem_.startX, problem_.startY,
        problem_.goalX,  problem_.goalY);

    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK_KERNEL();

    // Читаем результаты обратно
    std::vector<GPUSimResult> hostRes(N);
    CUDA_CHECK(cudaMemcpy(hostRes.data(), d_results_,
                          N * sizeof(GPUSimResult), cudaMemcpyDeviceToHost));

    std::vector<SimResult> out(N);
    for (int i = 0; i < N; ++i) {
        out[i].reachedGoal  = (hostRes[i].reachedGoal != 0);
        out[i].hitObstacle  = (hostRes[i].hitObstacle != 0);
        out[i].stepsUsed    = hostRes[i].stepsUsed;
        out[i].finalX       = hostRes[i].finalX;
        out[i].finalY       = hostRes[i].finalY;
        out[i].distToGoal   = hostRes[i].distToGoal;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Параллельный BFS на GPU
// ---------------------------------------------------------------------------
std::vector<Action> WorldModel::bfsGPU(int fromX, int fromY, int toX, int toY) const {
    if (problem_.isObstacle(fromX, fromY) || problem_.isObstacle(toX, toY))
        return {};

    int GG = G_ * G_;

    // Выделяем массивы на GPU
    int*    d_dist;
    int8_t* d_parent;
    int*    d_frontier[2];
    int*    d_frontierSize;

    CUDA_CHECK(cudaMalloc(&d_dist,         GG * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_parent,       GG * sizeof(int8_t)));
    CUDA_CHECK(cudaMalloc(&d_frontier[0],  GG * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_frontier[1],  GG * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_frontierSize, sizeof(int)));

    // Инициализация
    thrust::device_ptr<int>    distPtr(d_dist);
    thrust::fill(distPtr, distPtr + GG, (int)INF_DIST);

    thrust::device_ptr<int8_t> parentPtr(d_parent);
    thrust::fill(parentPtr, parentPtr + GG, (int8_t)-1);

    // Стартовая ячейка
    int startFlat = fromY * G_ + fromX;
    CUDA_CHECK(cudaMemset(d_dist + startFlat, 0, sizeof(int)));
    // dist[startFlat] = 0 — нужно записать 0 аккуратно
    int zero = 0;
    CUDA_CHECK(cudaMemcpy(d_dist + startFlat, &zero, sizeof(int), cudaMemcpyHostToDevice));

    // Инициализируем начальный фронтир
    CUDA_CHECK(cudaMemcpy(d_frontier[0], &startFlat, sizeof(int), cudaMemcpyHostToDevice));
    int initSize = 1;
    CUDA_CHECK(cudaMemcpy(d_frontierSize, &initSize, sizeof(int), cudaMemcpyHostToDevice));

    int curBuf = 0;
    int currentDist = 0;
    bool found = false;

    // BFS по уровням
    while (true) {
        int fSize = 0;
        CUDA_CHECK(cudaMemcpy(&fSize, d_frontierSize, sizeof(int), cudaMemcpyDeviceToHost));
        if (fSize == 0) break;

        // Проверяем, достигли ли цели
        int goalFlat = toY * G_ + toX;
        int goalDist;
        CUDA_CHECK(cudaMemcpy(&goalDist, d_dist + goalFlat, sizeof(int), cudaMemcpyDeviceToHost));
        if (goalDist != INF_DIST) { found = true; break; }

        // Обнуляем счётчик следующего фронтира
        int nextBuf = 1 - curBuf;
        CUDA_CHECK(cudaMemset(d_frontierSize, 0, sizeof(int)));

        int threads = RSADConfig::BFS_THREADS;
        int blocks  = (fSize + threads - 1) / threads;

        bfsFrontierKernel<<<blocks, threads>>>(
            d_frontier[curBuf], fSize,
            d_grid_, d_dist, d_parent,
            d_frontier[nextBuf], d_frontierSize,
            G_, currentDist);
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK_KERNEL();

        curBuf = nextBuf;
        currentDist++;

        // Защита от бесконечного цикла
        if (currentDist > G_ * G_) break;
    }

    std::vector<Action> path;

    if (found) {
        std::vector<int8_t> hostParent(GG);
        CUDA_CHECK(cudaMemcpy(hostParent.data(), d_parent,
                              GG * sizeof(int8_t), cudaMemcpyDeviceToHost));

        // BX/BY — дельты перемещения для каждого направления
        const int BX[4] = { 0, 0,-1, 1};   // LEFT=-1, RIGHT=+1
        const int BY[4] = {-1, 1, 0, 0};   // UP=-1,   DOWN=+1
        const int8_t RFWD[4] = {1, 0, 3, 2}; // прямое действие: RFWD[back] = d

        int cx = toX, cy = toY;
        std::vector<Action> revPath;

        while (cx != fromX || cy != fromY) {
            int flat = cy * G_ + cx;
            if (flat < 0 || flat >= GG) break;
            int8_t back = hostParent[flat];
            if (back < 0 || back > 3) break;
            // Прямое действие которым пришли из родителя
            revPath.push_back(static_cast<Action>(RFWD[back]));
            cx += BX[back];
            cy += BY[back];
        }

        std::reverse(revPath.begin(), revPath.end());
        path = revPath;
    }

    // Освобождаем GPU-память
    cudaFree(d_dist);
    cudaFree(d_parent);
    cudaFree(d_frontier[0]);
    cudaFree(d_frontier[1]);
    cudaFree(d_frontierSize);

    return path;
}

// ---------------------------------------------------------------------------
// Генерация вариантов обратного хода (Mode B)
// ---------------------------------------------------------------------------
std::vector<Solution> WorldModel::generateBackwardPaths(int numVariants) const {
    std::vector<Solution> result;

    // Основной путь (BFS оптимальный)
    auto basePath = bfsGPU(problem_.startX, problem_.startY,
                            problem_.goalX,  problem_.goalY);
    if (!basePath.empty()) {
        Solution s;
        s.actions = basePath;
        result.push_back(s);
    }

    if (basePath.empty()) {
        RSAD_LOG("[WorldModel] BFS found no path from (%d,%d) to (%d,%d)\n",
               problem_.startX, problem_.startY,
               problem_.goalX, problem_.goalY);
        return result;
    }

    // Генерируем вариации: случайные мутации базового пути
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> idxDist(0, (int)basePath.size() - 1);
    std::uniform_int_distribution<int> actDist(0, 3);

    for (int v = 1; v < numVariants; ++v) {
        Solution s;
        s.actions = basePath;

        // Мутируем несколько действий
        int mutations = 1 + v;
        for (int m = 0; m < mutations && !s.actions.empty(); ++m) {
            int idx = idxDist(rng) % static_cast<int>(s.actions.size());
            s.actions[idx] = static_cast<Action>(actDist(rng));
        }
        result.push_back(s);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Вспомогательные
// ---------------------------------------------------------------------------
float WorldModel::manhattanToGoal(int x, int y) const {
    return (float)(abs(x - problem_.goalX) + abs(y - problem_.goalY));
}

void WorldModel::printPath(const Solution& sol) const {
    // Воспроизводим путь на сетке (CPU)
    std::vector<std::vector<char>> grid(G_, std::vector<char>(G_, '.'));
    for (int y = 0; y < G_; ++y)
        for (int x = 0; x < G_; ++x)
            if (problem_.isObstacle(x, y)) grid[y][x] = '#';

    int x = problem_.startX, y = problem_.startY;
    grid[y][x] = 'S';
    for (auto a : sol.actions) {
        int nx = x, ny = y;
        switch (a) {
            case Action::UP:    ny--; break;
            case Action::DOWN:  ny++; break;
            case Action::LEFT:  nx--; break;
            case Action::RIGHT: nx++; break;
            default: break;
        }
        if (problem_.isObstacle(nx, ny)) break;
        x = nx; y = ny;
        if (!(x == problem_.goalX && y == problem_.goalY))
            grid[y][x] = '+';
    }
    grid[problem_.goalY][problem_.goalX] = 'G';

    RSAD_LOG("Path (len=%zu, score=%.2f):\n", sol.actions.size(), sol.totalScore);
    for (auto& row : grid) {
        for (char c : row) putchar(c);
        putchar('\n');
    }
}

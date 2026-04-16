#pragma once
// =============================================================================
// WorldModel.h — Мировая модель M: P × S → Results
//
//  Реализует GridWorld 10×10 с параллельной симуляцией батча решений на CUDA.
//  Обратная модель M^{-1} реализована через параллельный BFS на GPU.
// =============================================================================

#include "Utils.h"
#include <vector>
#include <cuda_runtime.h>

// ---------------------------------------------------------------------------
// Константы GPU-симуляции
// ---------------------------------------------------------------------------
#define MAX_BATCH_SIZE  4096   ///< Максимальный батч одновременных симуляций
#define INF_DIST        99999  ///< "Бесконечность" для BFS

// ---------------------------------------------------------------------------
// Плоская структура для GPU (только POD-типы)
// ---------------------------------------------------------------------------
struct GPUSolution {
    uint8_t actions[RSADConfig::MAX_SOL_LEN];  ///< Кодированные действия
    int32_t length;                            ///< Длина маршрута
};

struct GPUSimResult {
    int32_t finalX;
    int32_t finalY;
    int32_t stepsUsed;
    int32_t reachedGoal;   ///< 1 = достиг цели
    int32_t hitObstacle;   ///< 1 = попал в препятствие
    float   distToGoal;
};

// ---------------------------------------------------------------------------
// WorldModel
// ---------------------------------------------------------------------------
class WorldModel {
public:
    explicit WorldModel(const Problem& problem);
    ~WorldModel();

    // ---- Симуляция ----

    /// Симуляция одного решения (CPU, для отладки)
    SimResult simulateCPU(const Solution& sol) const;

    /// Симуляция батча решений на GPU — основной метод
    /// Возвращает вектор SimResult того же размера
    std::vector<SimResult> simulateBatch(const std::vector<Solution>& solutions) const;

    // ---- Обратная модель M^{-1}: GPU BFS ----

    /// Параллельный BFS от (fromX,fromY) до (toX,toY)
    /// Возвращает путь (последовательность Action) или пустой вектор
    std::vector<Action> bfsGPU(int fromX, int fromY, int toX, int toY) const;

    /// Генерация набора путей: обратный ход из цели (Mode B)
    /// Возвращает несколько вариантов пути, полученных BFS + вариации
    std::vector<Solution> generateBackwardPaths(int numVariants = 5) const;

    // ---- Доступ к проблеме ----
    const Problem& getProblem() const { return problem_; }

    /// Вычислить расстояние Манхэттен
    float manhattanToGoal(int x, int y) const;

    /// Распечатать путь на сетке
    void printPath(const Solution& sol) const;

private:
    Problem  problem_;
    uint8_t* d_grid_ = nullptr;   ///< GPU: карта препятствий [G*G]
    int      G_;                  ///< Размер сетки

    // GPU-буферы для пакетной симуляции (переиспользуются)
    mutable GPUSolution*  d_solutions_ = nullptr;
    mutable GPUSimResult* d_results_   = nullptr;
    mutable int           allocatedBatchSize_ = 0;

    void uploadGrid();
    void allocBatchBuffers(int n) const;
    void freeBatchBuffers() const;
};

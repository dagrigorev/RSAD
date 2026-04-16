#pragma once
// =============================================================================
// Utils.h — Общие типы, макросы и константы проекта РСАД
// =============================================================================

#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <cuda_runtime.h>

// ---------------------------------------------------------------------------
// Макрос проверки ошибок CUDA
// ---------------------------------------------------------------------------
#define CUDA_CHECK(err) do {                                             \
    cudaError_t _e = (err);                                              \
    if (_e != cudaSuccess) {                                             \
        fprintf(stderr, "CUDA error at %s:%d — %s\n",                   \
                __FILE__, __LINE__, cudaGetErrorString(_e));             \
        exit(EXIT_FAILURE);                                              \
    }                                                                    \
} while (0)

#define CUDA_CHECK_KERNEL() do {                                         \
    cudaError_t _e = cudaGetLastError();                                 \
    if (_e != cudaSuccess) {                                             \
        fprintf(stderr, "CUDA kernel error at %s:%d — %s\n",            \
                __FILE__, __LINE__, cudaGetErrorString(_e));             \
        exit(EXIT_FAILURE);                                              \
    }                                                                    \
} while (0)

// ---------------------------------------------------------------------------
// Константы по умолчанию
// ---------------------------------------------------------------------------
namespace RSADConfig {
    constexpr int   GRID_SIZE         = 10;     ///< Размер сетки GridWorld
    constexpr int   MAX_SOL_LEN       = 200;    ///< Максимальная длина решения (действий)
    constexpr int   EMBED_DIM         = 64;     ///< Размерность эмбеддинга концепции
    constexpr int   K_MIN             = 10;     ///< Минимальное число различимых решений
    constexpr float THRESHOLD_TOTAL   = 0.40f;  ///< Порог приемлемости решения
    constexpr float THRESHOLD_SIM     = 0.30f;  ///< Порог структурного сходства (Mode A)
    constexpr float H_MIN             = 0.50f;  ///< Порог энтропии разнообразия
    constexpr float DELTA_DIST        = 3.0f;   ///< Минимальная дистанция различимости
    constexpr int   MAX_ITER_SEARCH   = 5;      ///< Максимум итераций активного поиска
    constexpr int   BATCH_THREADS     = 256;    ///< Потоков CUDA на блок (симуляция)
    constexpr int   BFS_THREADS       = 128;    ///< Потоков CUDA на блок (BFS)
    // Веса оценщика (могут меняться динамически)
    constexpr float ALPHA1 = 0.40f;
    constexpr float ALPHA2 = 0.25f;
    constexpr float ALPHA3 = 0.20f;
    constexpr float ALPHA4 = 0.15f;
}

namespace RSADGlobal {
    extern bool verbose;  // объявление — не определение
}
#define RSAD_LOG(...) do { if (RSADGlobal::verbose) printf(__VA_ARGS__); } while(0)

// ---------------------------------------------------------------------------
// Действия агента в GridWorld
// ---------------------------------------------------------------------------
enum class Action : uint8_t {
    UP    = 0,
    DOWN  = 1,
    LEFT  = 2,
    RIGHT = 3,
    NONE  = 4
};

inline char actionToChar(Action a) {
    switch (a) {
        case Action::UP:    return 'U';
        case Action::DOWN:  return 'D';
        case Action::LEFT:  return 'L';
        case Action::RIGHT: return 'R';
        default:            return '?';
    }
}

inline Action charToAction(char c) {
    switch (c) {
        case 'U': return Action::UP;
        case 'D': return Action::DOWN;
        case 'L': return Action::LEFT;
        case 'R': return Action::RIGHT;
        default:  return Action::NONE;
    }
}

// ---------------------------------------------------------------------------
// Структура решения
// ---------------------------------------------------------------------------
struct Solution {
    std::vector<Action> actions;   ///< Последовательность действий

    // Метрики оценщика (заполняются Evaluator)
    float e1 = 0.0f;  ///< Степень удовлетворения требования
    float e2 = 0.0f;  ///< Перспективность
    float e3 = 0.0f;  ///< Элегантность (обратная сложность)
    float e4 = 0.0f;  ///< Отсутствие побочных эффектов
    float totalScore = 0.0f;

    bool valid = false;  ///< Прошло ли порог

    /// Строковое представление: "UURRDD..."
    std::string toString() const {
        std::string s;
        s.reserve(actions.size());
        for (auto a : actions) s += actionToChar(a);
        return s;
    }

    /// Компактное представление для GPU (uint8_t массив)
    void toPackedArray(uint8_t* buf, int maxLen) const {
        int n = static_cast<int>(actions.size());
        int len = std::min(n, maxLen);
        for (int i = 0; i < len; ++i)
            buf[i] = static_cast<uint8_t>(actions[i]);
        for (int i = len; i < maxLen; ++i)
            buf[i] = static_cast<uint8_t>(Action::NONE);
    }

    static Solution fromPackedArray(const uint8_t* buf, int len) {
        Solution s;
        for (int i = 0; i < len; ++i) {
            if (buf[i] == static_cast<uint8_t>(Action::NONE)) break;
            s.actions.push_back(static_cast<Action>(buf[i]));
        }
        return s;
    }
};

// ---------------------------------------------------------------------------
// Структура проблемы (сеточный мир)
// ---------------------------------------------------------------------------
struct Problem {
    int gridSize  = RSADConfig::GRID_SIZE;
    int startX = 0, startY = 0;
    int goalX  = 9, goalY  = 9;
    std::vector<bool> obstacles;  ///< Плоский массив [y * gridSize + x]

    bool isObstacle(int x, int y) const {
        if (x < 0 || y < 0 || x >= gridSize || y >= gridSize) return true;
        return obstacles[y * gridSize + x];
    }

    void setObstacle(int x, int y, bool v) {
        obstacles[y * gridSize + x] = v;
    }

    /// Инициализация пустой сетки
    void init(int size) {
        gridSize = size;
        obstacles.assign(size * size, false);
    }

    /// Случайная расстановка препятствий (density = 0..1)
    void randomObstacles(float density, unsigned seed = 42) {
        std::mt19937 rng(seed);
        std::bernoulli_distribution dist(density);
        for (int y = 0; y < gridSize; ++y)
            for (int x = 0; x < gridSize; ++x) {
                // Не ставим на старт и цель
                if ((x == startX && y == startY) || (x == goalX && y == goalY))
                    continue;
                obstacles[y * gridSize + x] = dist(rng);
            }
    }

    void print() const {
        for (int y = 0; y < gridSize; ++y) {
            for (int x = 0; x < gridSize; ++x) {
                if (x == startX && y == startY)     putchar('S');
                else if (x == goalX && y == goalY)  putchar('G');
                else if (isObstacle(x, y))           putchar('#');
                else                                 putchar('.');
            }
            putchar('\n');
        }
    }
};

// ---------------------------------------------------------------------------
// Результат симуляции одного решения
// ---------------------------------------------------------------------------
struct SimResult {
    bool    reachedGoal  = false;
    bool    hitObstacle  = false;
    int     stepsUsed    = 0;
    int     finalX       = 0;
    int     finalY       = 0;
    float   distToGoal   = 0.0f;  ///< Евклидова дистанция до цели
};

// ---------------------------------------------------------------------------
// Вспомогательные утилиты
// ---------------------------------------------------------------------------
namespace Utils {

inline float editDistance(const Solution& a, const Solution& b) {
    // Расстояние Левенштейна на действиях
    int m = static_cast<int>(a.actions.size());
    int n = static_cast<int>(b.actions.size());
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;
    for (int i = 1; i <= m; ++i)
        for (int j = 1; j <= n; ++j) {
            int cost = (a.actions[i-1] != b.actions[j-1]) ? 1 : 0;
            dp[i][j] = std::min({dp[i-1][j]+1, dp[i][j-1]+1, dp[i-1][j-1]+cost});
        }
    return static_cast<float>(dp[m][n]);
}

/// Вычисление K_eff — количество различимых решений (dist > delta)
inline int computeKeff(const std::vector<Solution>& pool, float delta) {
    std::vector<int> selected;
    for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
        bool distinct = true;
        for (int j : selected) {
            if (editDistance(pool[i], pool[j]) <= delta) { distinct = false; break; }
        }
        if (distinct) selected.push_back(i);
    }
    return static_cast<int>(selected.size());
}

/// Вычисление энтропии распределения оценок
inline float computeEntropy(const std::vector<Solution>& pool) {
    if (pool.empty()) return 0.0f;
    // softmax веса
    float maxS = -1e9f;
    for (auto& s : pool) maxS = std::max(maxS, s.totalScore);
    std::vector<float> weights(pool.size());
    float sum = 0.0f;
    for (int i = 0; i < (int)pool.size(); ++i) {
        weights[i] = std::exp(pool[i].totalScore - maxS);
        sum += weights[i];
    }
    float H = 0.0f;
    for (float w : weights) {
        float p = w / sum;
        if (p > 1e-9f) H -= p * std::log(p);
    }
    return H;
}

inline double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()).count();
}

} // namespace Utils

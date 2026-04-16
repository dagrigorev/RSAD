#pragma once
// =============================================================================
// BackwardChainer.h — Настоящий Mode B: обратная трассировка через M^{-1}
//
//  Теория (раздел 3.2):
//    Задача: найти S₁, S₂, …, Sₜ такие, что
//      M(P, S₁) = Q₁;  M(Q₁, S₂) = Q₂;  …;  M(Q_{t-1}, Sₜ) = Q
//
//  Реализация:
//    1. Генерируем K стратегий декомпозиции пути (наборов промежуточных целей).
//    2. Для каждой стратегии решаем каждый отрезок через M^{-1}:
//         M^{-1}(state, action) = predecessor (для GridWorld — инверсия действия)
//    3. Каждый отрезок — отдельная задача планирования с BFS через обратный граф.
//    4. Склеиваем отрезки в целостное решение.
//
//  Это принципиально отличается от:
//    - Mode A: ищет аналоги в памяти
//    - ActiveSearcher: случайные мутации готовых путей
//    - Текущий Mode B: прямой BFS + случайные возмущения
// =============================================================================

#include "Utils.h"
#include "WorldModel.h"
#include <vector>
#include <tuple>
#include <functional>

// ---------------------------------------------------------------------------
// Стратегия выбора промежуточных точек (waypoints)
// ---------------------------------------------------------------------------
enum class WaypointStrategy {
    DIAGONAL,       ///< Через диагональные четверти сетки
    PERIMETER,      ///< Вдоль периметра (обходной маршрут)
    OBSTACLE_AVOID, ///< Через "проходы" между скоплениями препятствий
    RANDOM_INTERIOR,///< Случайные внутренние точки
    QUADRANT_CENTERS///< Центры четырёх квадрантов
};

// ---------------------------------------------------------------------------
// Один отрезок декомпозиции
// ---------------------------------------------------------------------------
struct PathSegment {
    int fromX, fromY;
    int toX,   toY;
    std::vector<Action> actions;  ///< Найденный путь (пустой = нет пути)
    bool reachable = false;
};

// ---------------------------------------------------------------------------
// Результат обратной трассировки
// ---------------------------------------------------------------------------
struct BackwardTrace {
    std::vector<PathSegment>     segments;
    std::vector<std::pair<int,int>> waypoints; ///< Промежуточные цели
    WaypointStrategy             strategy;
    bool                         complete = false; ///< Все отрезки найдены
    Solution                     solution;
};

// ---------------------------------------------------------------------------
// Класс обратной трассировки
// ---------------------------------------------------------------------------
class BackwardChainer {
public:
    explicit BackwardChainer(const WorldModel& world);

    // ── Основной метод ──────────────────────────────────────────────────────
    /// Генерировать numVariants решений через разные стратегии декомпозиции
    std::vector<Solution> generate(const Problem& problem, int numVariants = 8) const;

    // ── Отдельные стратегии ─────────────────────────────────────────────────
    BackwardTrace traceDiagonal       (const Problem& p) const;
    BackwardTrace tracePerimeter      (const Problem& p) const;
    BackwardTrace traceObstacleAware  (const Problem& p) const;
    BackwardTrace traceRandomInterior (const Problem& p, unsigned seed) const;
    BackwardTrace traceQuadrantCenters(const Problem& p) const;

private:
    const WorldModel& world_;

    // ── M^{-1} для GridWorld ────────────────────────────────────────────────
    /// Обратное применение действия: из какой клетки нужно выйти,
    /// применив действие `a`, чтобы оказаться в (x, y)?
    std::pair<int,int> inverseApply(int x, int y, Action a) const;

    /// Прямое применение действия (для проверки)
    std::pair<int,int> forwardApply(int x, int y, Action a) const;

    // ── Планирование одного отрезка ─────────────────────────────────────────
    /// BFS через обратный граф: от toX,toY назад до fromX,fromY
    /// Возвращает последовательность ПРЯМЫХ действий (от from до to)
    std::vector<Action> solveSegment(int fromX, int fromY,
                                      int toX,   int toY,
                                      const Problem& p) const;

    /// Собрать BackwardTrace по набору waypoints
    BackwardTrace solveWithWaypoints(
        const Problem& p,
        const std::vector<std::pair<int,int>>& wps,
        WaypointStrategy strategy) const;

    // ── Вспомогательные ─────────────────────────────────────────────────────
    /// Найти "проход" — свободную клетку рядом с узким местом
    std::pair<int,int> findCorridor(const Problem& p, int regionX, int regionY) const;

    /// Оценить "заселённость" препятствиями в регионе
    float obstacleDesity(const Problem& p, int cx, int cy, int r) const;

    /// Клэмп координат в границы сетки (не на препятствие)
    std::pair<int,int> clampFree(const Problem& p, int x, int y) const;
};

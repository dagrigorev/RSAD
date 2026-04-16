#pragma once
// =============================================================================
// MemoryPruner.h — Отсечение памяти (pruning) гиперграфа
//
//  После N эпизодов память накапливает сотни рёбер.
//  Pruner оставляет только:
//   1. Рёбра с высоким весом (часто использовавшиеся)
//   2. Структурно разнообразные рёбра (не дублирующие друг друга)
//   3. Системные рёбра (ANALOGY, CAUSALITY) — никогда не удаляются
// =============================================================================
#include "HypergraphMemory.h"

class MemoryPruner {
public:
    struct Config {
        int   maxEdges         = 200;   ///< Максимум рёбер в памяти
        float minWeight        = 0.3f;  ///< Минимальный вес ребра
        float minSolutionQuality = 0.5f;///< Минимальное качество прикреплённого решения
        bool  keepStructural   = true;  ///< Сохранять ANALOGY/CAUSALITY рёбра
    };

    explicit MemoryPruner() {}
    explicit MemoryPruner(const Config& cfg) : cfg_(cfg) {}

    /// Выполнить отсечение. Возвращает число удалённых рёбер.
    int prune(HypergraphMemory& memory);

    void setConfig(const Config& c) { cfg_ = c; }

private:
    Config cfg_;

    // Оценка важности ребра
    float scoreEdge(const Hyperedge& e) const;
};

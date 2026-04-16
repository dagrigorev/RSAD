// =============================================================================
// MemoryPruner.cpp — Реализация отсечения памяти
// =============================================================================
#include "MemoryPruner.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include <numeric>

float MemoryPruner::scoreEdge(const Hyperedge& e) const {
    float score = e.weight;

    // Бонус за прикреплённое решение высокого качества
    if (e.hasSolution)
        score += e.solution.totalScore * 0.5f;

    // Бонус за структурные отношения (знания, а не просто решения)
    switch (e.relType) {
        case RelationType::ANALOGY:       score += 0.3f; break;
        case RelationType::CAUSALITY:     score += 0.3f; break;
        case RelationType::PART_WHOLE:    score += 0.2f; break;
        case RelationType::PRECEDENCE:    score += 0.2f; break;
        case RelationType::SIMILAR_STRUCT:score += 0.1f; break;
        default: break;
    }

    // Штраф за очень длинные/короткие решения (неэлегантные)
    if (e.hasSolution) {
        int len = (int)e.solution.actions.size();
        if (len > RSADConfig::MAX_SOL_LEN * 0.7f) score -= 0.2f;
        if (len == 0)                              score -= 1.0f;
    }

    return score;
}

int MemoryPruner::prune(HypergraphMemory& memory) {
    auto& edges = const_cast<std::vector<Hyperedge>&>(memory.getEdges());
    int before = (int)edges.size();

    if (before <= cfg_.maxEdges) return 0;  // Ничего делать не нужно

    // 1. Оцениваем все рёбра
    std::vector<std::pair<float, int>> scored;
    scored.reserve(before);
    for (int i = 0; i < before; i++)
        scored.emplace_back(scoreEdge(edges[i]), i);

    // 2. Сортируем по убыванию оценки
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b){ return a.first > b.first; });

    // 3. Помечаем рёбра для сохранения
    std::vector<bool> keep(before, false);

    int kept = 0;
    for (auto& [score, idx] : scored) {
        const auto& e = edges[idx];

        // Всегда сохраняем структурные рёбра
        bool isStructural = (e.relType == RelationType::ANALOGY ||
                             e.relType == RelationType::CAUSALITY ||
                             e.relType == RelationType::PART_WHOLE);
        if (cfg_.keepStructural && isStructural) {
            keep[idx] = true;
            kept++;
            continue;
        }

        // Отсекаем по минимальному весу
        if (e.weight < cfg_.minWeight) continue;

        // Отсекаем по качеству решения
        if (e.hasSolution && e.solution.totalScore < cfg_.minSolutionQuality)
            continue;

        // Отсекаем пустые решения
        if (e.hasSolution && e.solution.actions.empty()) continue;

        if (kept < cfg_.maxEdges) {
            keep[idx] = true;
            kept++;
        }
    }

    // 4. Строим новый список рёбер, переиндексируя IDs
    std::vector<Hyperedge> newEdges;
    newEdges.reserve(kept);
    uint32_t newId = 0;
    for (int i = 0; i < before; i++) {
        if (keep[i]) {
            edges[i].id = newId++;
            newEdges.push_back(edges[i]);
        }
    }

    edges = std::move(newEdges);

    int removed = before - (int)edges.size();
    if (removed > 0)
        RSAD_LOG("[MemoryPruner] Pruned %d edges (%d → %d), kept %.1f%%\n",
                 removed, before, (int)edges.size(),
                 100.0f * edges.size() / before);
    return removed;
}

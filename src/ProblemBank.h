#pragma once
// =============================================================================
// ProblemBank.h — Банк задач для вычисления истинной e2 (перспективности)
//
//  Теория (раздел 4):
//    e2 = |{P' ∈ P_known | S применимо к P' и снижает энтропию}| / |P_known|
//
//  Реализация:
//    - 30 задач разной сложности (сетки 4×4 … 14×14, 0–35% препятствий)
//    - Разные конфигурации старта/цели (не только угол–угол)
//    - Для каждого нового решения S: simulateBatch на всём банке
//    - e2 = доля задач, где S улучшает позицию (distToGoal ↓) или решает
// =============================================================================

#include "Utils.h"
#include "WorldModel.h"
#include <vector>
#include <memory>

class ProblemBank {
public:
    ProblemBank();

    // ── Оценка перспективности ───────────────────────────────────────────────
    /// Запустить решение на всём банке, вернуть долю задач где оно улучшает ситуацию
    /// (достигает цели ИЛИ дистанция до цели < manhattan(start,goal)/2)
    float evaluatePerspectiveness(const Solution& sol) const;

    /// Пакетная оценка пула решений (для батчевого вычисления e2)
    std::vector<float> evaluateBatch(const std::vector<Solution>& pool) const;

    size_t size() const { return problems_.size(); }
    const Problem& getProblem(int i) const { return problems_[i]; }

    void print() const;

private:
    std::vector<Problem>                  problems_;
    std::vector<std::unique_ptr<WorldModel>> worlds_;

    void generate();
    void addProblem(int gridSize, int sx, int sy, int gx, int gy,
                    float density, unsigned seed);
};

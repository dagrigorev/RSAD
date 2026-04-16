// =============================================================================
// ProblemBank.cpp — Реализация банка задач для оценки перспективности
// =============================================================================
#include "ProblemBank.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <numeric>

ProblemBank::ProblemBank() { generate(); }

// ---------------------------------------------------------------------------
// Добавить задачу в банк
// ---------------------------------------------------------------------------
void ProblemBank::addProblem(int gridSize, int sx, int sy, int gx, int gy,
                              float density, unsigned seed) {
    Problem p;
    p.init(gridSize);
    p.startX = sx; p.startY = sy;
    p.goalX  = gx; p.goalY  = gy;
    // Не ставим препятствия на старт и цель
    p.randomObstacles(density, seed);
    // Защита от непроходимых лабиринтов: если плотность высокая,
    // убираем препятствия вдоль главной диагонали
    if (density > 0.25f) {
        for (int i = 0; i < gridSize; i++) {
            p.setObstacle(i, i, false);
            if (i+1 < gridSize) p.setObstacle(i, i+1, false);
        }
    }
    problems_.push_back(p);
    worlds_.push_back(std::make_unique<WorldModel>(p));
}

// ---------------------------------------------------------------------------
// Генерация банка задач (30 задач, 5 групп сложности)
// ---------------------------------------------------------------------------
void ProblemBank::generate() {
    // ── Группа 1: Тривиальные (4×4, 5×5, без/мало препятствий) ─────────────
    addProblem(4, 0,0, 3,3, 0.00f, 101);
    addProblem(5, 0,0, 4,4, 0.05f, 102);
    addProblem(5, 0,2, 4,2, 0.08f, 103);   // горизонтальный путь
    addProblem(4, 0,3, 3,0, 0.05f, 104);   // диагональ против хода

    // ── Группа 2: Лёгкие (6×6–7×7, 10–15% препятствий) ────────────────────
    addProblem(6, 0,0, 5,5, 0.10f, 201);
    addProblem(6, 1,0, 5,4, 0.12f, 202);
    addProblem(7, 0,0, 6,6, 0.12f, 203);
    addProblem(7, 0,3, 6,3, 0.10f, 204);   // горизонтальный
    addProblem(6, 0,0, 5,3, 0.15f, 205);   // неполная диагональ
    addProblem(7, 2,0, 4,6, 0.12f, 206);   // нестандартный старт

    // ── Группа 3: Средние (8×8–10×10, 15–25% препятствий) ──────────────────
    addProblem(8,  0,0, 7,7, 0.18f, 301);
    addProblem(8,  0,0, 7,4, 0.20f, 302);
    addProblem(9,  0,0, 8,8, 0.20f, 303);
    addProblem(9,  1,1, 7,7, 0.22f, 304);
    addProblem(10, 0,0, 9,9, 0.20f, 305);
    addProblem(10, 0,0, 9,5, 0.18f, 306);
    addProblem(10, 2,2, 8,8, 0.20f, 307);
    addProblem(8,  0,4, 7,4, 0.25f, 308);   // горизонталь с препятствиями

    // ── Группа 4: Сложные (11×11–13×13, 25–30% препятствий) ────────────────
    addProblem(11, 0,0, 10,10, 0.25f, 401);
    addProblem(12, 0,0, 11,11, 0.28f, 402);
    addProblem(12, 1,0, 10,11, 0.28f, 403);
    addProblem(13, 0,0, 12,12, 0.27f, 404);
    addProblem(11, 0,0, 10, 5, 0.28f, 405);
    addProblem(12, 2,2, 10,10, 0.30f, 406);

    // ── Группа 5: Очень сложные (14×14, 28–35%) ─────────────────────────────
    addProblem(14, 0,0, 13,13, 0.28f, 501);
    addProblem(14, 0,0, 13, 7, 0.30f, 502);
    addProblem(14, 1,1, 12,12, 0.30f, 503);
    addProblem(14, 0,7, 13, 7, 0.28f, 504);   // горизонталь через всё поле
    addProblem(14, 2,0, 12,13, 0.33f, 505);
    addProblem(14, 0,0, 13,13, 0.35f, 506);   // максимальная плотность

    RSAD_LOG("[ProblemBank] Generated %zu diverse problems (5 difficulty groups)\n",
             problems_.size());
}

// ---------------------------------------------------------------------------
// Оценка перспективности одного решения на всём банке
// ---------------------------------------------------------------------------
float ProblemBank::evaluatePerspectiveness(const Solution& sol) const {
    if (sol.actions.empty() || problems_.empty()) return 0.0f;

    int improved = 0;

    for (int i = 0; i < (int)problems_.size(); i++) {
        const Problem& p  = problems_[i];
        SimResult res = worlds_[i]->simulateCPU(sol);

        bool useful = false;

        // Критерий 1: достигли цели
        if (res.reachedGoal) {
            useful = true;
        }
        // Критерий 2: приблизились к цели значительно
        else {
            float startDist = std::hypotf(
                (float)(p.startX - p.goalX),
                (float)(p.startY - p.goalY));
            float improvement = startDist - res.distToGoal;
            // "Снижает энтропию": улучшение > 30% исходной дистанции
            if (improvement > startDist * 0.30f) {
                useful = true;
            }
        }

        if (useful) improved++;
    }

    return (float)improved / (float)problems_.size();
}

// ---------------------------------------------------------------------------
// Пакетная оценка пула решений
// ---------------------------------------------------------------------------
std::vector<float> ProblemBank::evaluateBatch(
    const std::vector<Solution>& pool) const
{
    std::vector<float> results(pool.size());

    // Для каждой задачи банка — симуляция всего пула (используем simulateBatch)
    // Накапливаем счёт по решениям
    std::vector<int> scores(pool.size(), 0);

    for (int bi = 0; bi < (int)problems_.size(); bi++) {
        const Problem& p = problems_[bi];

        // Симуляция всего пула на задаче bi
        auto sims = worlds_[bi]->simulateBatch(pool);

        float startDist = std::hypotf(
            (float)(p.startX - p.goalX),
            (float)(p.startY - p.goalY));

        for (int si = 0; si < (int)pool.size(); si++) {
            bool useful = sims[si].reachedGoal ||
                         (startDist - sims[si].distToGoal > startDist * 0.30f);
            if (useful) scores[si]++;
        }
    }

    float N = (float)problems_.size();
    for (int i = 0; i < (int)pool.size(); i++)
        results[i] = (float)scores[i] / N;

    return results;
}

void ProblemBank::print() const {
    printf("\n[ProblemBank] %zu problems:\n", problems_.size());
    for (int i = 0; i < (int)problems_.size(); i++) {
        auto& p = problems_[i];
        int obs = 0;
        for (bool b : p.obstacles) if(b) obs++;
        float dens = (float)obs / (p.gridSize*p.gridSize);
        printf("  [%2d] %2dx%2d  (%d,%d)→(%d,%d)  obs=%.0f%%\n",
               i, p.gridSize, p.gridSize,
               p.startX, p.startY, p.goalX, p.goalY, dens*100);
    }
}

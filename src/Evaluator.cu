// =============================================================================
// Evaluator_cpu.cpp — CPU реализация оценщика (заменяет CUDA ядро)
// =============================================================================
#include "Evaluator.h"
#include <cmath>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <numeric>

#ifdef _OPENMP
#  include <omp.h>
#endif

Evaluator::Evaluator(float a1, float a2, float a3, float a4)
    : alpha1_(a1), alpha2_(a2), alpha3_(a3), alpha4_(a4) {}

Evaluator::~Evaluator() {}

void Evaluator::setAlphas(float a1, float a2, float a3, float a4) {
    alpha1_=a1; alpha2_=a2; alpha3_=a3; alpha4_=a4;
}
void Evaluator::allocGPU(int) const {}
void Evaluator::freeGPU() const {}

// ---------------------------------------------------------------------------
// CPU вычисление e1 (аналог CUDA ядра evaluateKernel)
// ---------------------------------------------------------------------------
static void computeE1E4(const EvalInput& inp, EvalOutput& out) {
    float e1 = 0.0f;
    if (inp.reachedGoal) {
        float stepRatio = (inp.maxSteps > 0)
            ? 1.0f - (float)inp.stepsUsed / (float)(inp.maxSteps*2)
            : 0.5f;
        e1 = 0.7f + 0.3f * std::max(0.0f, stepRatio);
    } else if (!inp.hitObstacle) {
        float dr = (inp.maxDist>0) ? 1.0f - inp.distToGoal/inp.maxDist : 0.0f;
        e1 = 0.3f * std::max(0.0f, dr);
    } else {
        float dr = (inp.maxDist>0) ? 1.0f - inp.distToGoal/inp.maxDist : 0.0f;
        e1 = 0.1f * std::max(0.0f, dr);
    }
    float harm = 0.0f;
    if (inp.hitObstacle) harm += 0.5f;
    if (inp.maxSteps > 0) {
        float lp = std::min(1.0f, (float)inp.solutionLen/(float)(inp.maxSteps*3));
        harm += 0.3f * lp;
    }
    out.e1 = std::max(0.0f, std::min(1.0f, e1));
    out.e4 = 1.0f - std::min(1.0f, harm);
}

float Evaluator::computeE2(const Solution& sol, const SimResult& res,
                             const Problem& problem) const {
    // ── Истинная перспективность (теория, раздел 4) ─────────────────────────
    // e2 = |{P' ∈ P_known | S применимо к P' и снижает энтропию}| / |P_known|
    if (problemBank_ != nullptr && problemBank_->size() > 0) {
        return problemBank_->evaluatePerspectiveness(sol);
    }

    // ── Fallback: эффективность пути + базовая обобщаемость ─────────────────
    // Используется до инициализации банка задач.
    // e2 = смесь: эффективность × достижение × нормированная длина
    if (!res.reachedGoal) {
        // Не достигли цели: перспективность пропорциональна прогрессу
        float startDist = (float)(abs(problem.startX-problem.goalX)+
                                   abs(problem.startY-problem.goalY));
        float improvement = startDist - res.distToGoal;
        return std::max(0.0f, std::min(0.35f, improvement / std::max(1.0f, startDist)));
    }
    float minLen = (float)(abs(problem.startX-problem.goalX)+
                            abs(problem.startY-problem.goalY));
    float actual = (float)res.stepsUsed;
    // Эффективность решения + штраф за избыточную длину
    float efficiency = (actual>0) ? minLen/actual : 0.0f;
    // Короткое решение = более обобщаемое (применимо к похожим задачам)
    float lenBonus = std::max(0.0f, 1.0f - actual/(minLen*3.0f));
    return std::min(1.0f, 0.6f*efficiency + 0.4f*lenBonus);
}

float Evaluator::computeE3(const Solution& sol) const {
    if (sol.actions.empty()) return 1.0f;
    int len = (int)sol.actions.size();
    int transitions = 0;
    for (int i=1; i<len; ++i)
        if (sol.actions[i]!=sol.actions[i-1]) transitions++;
    float K    = (float)len * (1.0f + 0.5f*(float)transitions/(float)len);
    float Kmax = RSADConfig::MAX_SOL_LEN * 1.5f;
    return 1.0f / (1.0f + (K/Kmax)*10.0f);
}

void Evaluator::evaluate(std::vector<Solution>& pool,
                          const std::vector<SimResult>& simResults,
                          const Problem& problem) const
{
    int N = (int)pool.size();
    assert((int)simResults.size() == N);
    if (N==0) return;

    float maxDist  = (float)(problem.gridSize*2);
    int   maxSteps = problem.gridSize*4;

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic,16)
#endif
    for (int i=0; i<N; ++i) {
        EvalInput inp;
        inp.stepsUsed   = simResults[i].stepsUsed;
        inp.maxSteps    = maxSteps;
        inp.reachedGoal = simResults[i].reachedGoal ? 1 : 0;
        inp.hitObstacle = simResults[i].hitObstacle ? 1 : 0;
        inp.distToGoal  = simResults[i].distToGoal;
        inp.maxDist     = maxDist;
        inp.solutionLen = (int)pool[i].actions.size();

        EvalOutput out;
        computeE1E4(inp, out);
        pool[i].e1 = out.e1;
        pool[i].e4 = out.e4;
        pool[i].e2 = computeE2(pool[i], simResults[i], problem);
        pool[i].e3 = computeE3(pool[i]);
        pool[i].totalScore =
            alpha1_*pool[i].e1 + alpha2_*pool[i].e2 +
            alpha3_*pool[i].e3 + alpha4_*pool[i].e4;
        pool[i].valid = (pool[i].totalScore >= RSADConfig::THRESHOLD_TOTAL);
    }

    // ── Пакетная оценка e2 через ProblemBank (одна симуляция на весь пул) ──
    if (problemBank_ != nullptr && problemBank_->size() > 0) {
        auto e2vals = problemBank_->evaluateBatch(pool);
        for (int i=0;i<N;++i) {
            pool[i].e2 = e2vals[i];
            // Пересчитываем totalScore с новым e2
            pool[i].totalScore =
                alpha1_*pool[i].e1 + alpha2_*pool[i].e2 +
                alpha3_*pool[i].e3 + alpha4_*pool[i].e4;
            pool[i].valid = (pool[i].totalScore >= RSADConfig::THRESHOLD_TOTAL);
        }
    }
}
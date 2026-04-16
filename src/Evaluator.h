#pragma once
// =============================================================================
// Evaluator.h — Оценщик решений (e1..e4, E_total)
//
//  e1 = 1 - ε(P,S)        — удовлетворение требования (из симуляции)
//  e2                      — перспективность (по памяти)
//  e3 = 1/(1+K(S))         — элегантность (обратная к сложности)
//  e4 = 1 - harm(P,S)      — отсутствие побочных ухудшений
//
//  Вычисление e1/e4 на GPU (пакетно), e2/e3 — CPU.
// =============================================================================

#include "Utils.h"
#include "WorldModel.h"
#include "ProblemBank.h"
#include <vector>

// GPU POD: входные данные для оценки одного решения
struct EvalInput {
    int32_t stepsUsed;
    int32_t maxSteps;
    int32_t reachedGoal;
    int32_t hitObstacle;
    float   distToGoal;
    float   maxDist;
    int32_t solutionLen;
};

// GPU POD: результат оценки
struct EvalOutput {
    float e1;
    float e4;
};

class Evaluator {
public:
    Evaluator(float alpha1 = RSADConfig::ALPHA1,
              float alpha2 = RSADConfig::ALPHA2,
              float alpha3 = RSADConfig::ALPHA3,
              float alpha4 = RSADConfig::ALPHA4);
    ~Evaluator();

    /// Оценить пул решений: заполняет e1..e4 и totalScore для каждого
    void evaluate(std::vector<Solution>& pool,
                  const std::vector<SimResult>& simResults,
                  const Problem& problem) const;

    /// Обновить веса адаптивно (например, усилить исследование)
    void setAlphas(float a1, float a2, float a3, float a4);
    void setProblemBank(const ProblemBank* bank) { problemBank_ = bank; }

    float getAlpha1() const { return alpha1_; }
    float getAlpha2() const { return alpha2_; }
    float getAlpha3() const { return alpha3_; }
    float getAlpha4() const { return alpha4_; }

private:
    float alpha1_, alpha2_, alpha3_, alpha4_;
    const ProblemBank* problemBank_ = nullptr;

    EvalInput*  d_inputs_  = nullptr;
    EvalOutput* d_outputs_ = nullptr;
    int         allocN_    = 0;

    void allocGPU(int n) const;
    void freeGPU() const;

    // e2: перспективность — упрощённая оценка по длине пути
    float computeE2(const Solution& sol, const SimResult& res,
                    const Problem& problem) const;

    // e3: элегантность — аппроксимация Колмогорова
    float computeE3(const Solution& sol) const;
};

#pragma once
// =============================================================================
// ActiveSearcher.h — Активный поиск информации (раздел 6 теории РСАД)
//
//  Когда K_eff < K_min, агент генерирует новые решения через:
//  1. Мутацию существующих решений
//  2. Случайные блуждания по GridWorld
//  3. Кросс-брид двух решений
//  4. Запросы к LLM (через ModeC)
//
//  Целевая функция: q* = argmax_q (ΔK(q) - λ·cost(q))
// =============================================================================

#include "Utils.h"
#include "WorldModel.h"
#include "HypergraphMemory.h"
#include "LLMAdvisor.h"
#include <vector>
#include <memory>
#include <random>
#include <functional>

// Тип запроса
enum class QueryType {
    MUTATION,     ///< Мутация существующего решения
    RANDOM_WALK,  ///< Случайное блуждание
    CROSSOVER,    ///< Скрещивание двух решений
    LLM_QUERY,    ///< Запрос к языковой модели
    GRAPH_EXPLORE ///< Обход нового участка пространства решений
};

// Описание запроса
struct SearchQuery {
    QueryType  type;
    float      expectedDeltaK; ///< Ожидаемый прирост K_eff
    float      cost;           ///< Стоимость выполнения
    float      utility;        ///< expectedDeltaK - λ * cost

    int        parentIdx1 = -1;  ///< Индекс в пуле для мутации/crossover
    int        parentIdx2 = -1;
    unsigned   seed = 0;
};

class ActiveSearcher {
public:
    ActiveSearcher(const WorldModel& world,
                   HypergraphMemory& memory,
                   float lambda = 0.1f);
    void setLLMCallback(std::function<std::string(const std::string&)> cb) {
        advisor_.setLLMCallback(cb);
    }

    /// Основной метод: расширить пул до K_min различимых решений
    /// Возвращает новые решения для добавления в пул
    std::vector<Solution> search(
        const std::vector<Solution>& currentPool,
        const Problem& problem,
        int kMin,
        int maxIterations = RSADConfig::MAX_ITER_SEARCH);

    void setLambda(float l) { lambda_ = l; }

private:
    const WorldModel& world_;
    HypergraphMemory& memory_;
    float             lambda_;
    std::mt19937      rng_;
    LLMAdvisor        advisor_;  ///< LLM-советник

    // ---- Генераторы новых решений ----
    Solution mutate(const Solution& src, float rate, unsigned seed);
    Solution crossover(const Solution& s1, const Solution& s2, unsigned seed);
    Solution randomWalk(const Problem& problem, unsigned seed, int maxLen = 80);
    Solution greedyWalk(const Problem& problem, unsigned seed, float noiseLevel = 0.2f);

    // ---- Оценка запросов ----
    std::vector<SearchQuery> planQueries(
        const std::vector<Solution>& pool,
        const Problem& problem,
        int numQueries = 20);

    float estimateDeltaK(const SearchQuery& q,
                          const std::vector<Solution>& pool,
                          const Problem& problem);

    // ---- Выполнение запроса ----
    std::vector<Solution> executeQuery(const SearchQuery& q,
                                        const std::vector<Solution>& pool,
                                        const Problem& problem,
                                        int batchSize = 8);
};

// =============================================================================
// ActiveSearcher.cu — Реализация активного поиска информации
// =============================================================================
#include "ActiveSearcher.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>

ActiveSearcher::ActiveSearcher(const WorldModel& world,
                                 HypergraphMemory& memory,
                                 float lambda)
    : world_(world), memory_(memory), lambda_(lambda), rng_(42),
      advisor_(nullptr, &world) {}

// ---------------------------------------------------------------------------
// Мутация решения
// ---------------------------------------------------------------------------
Solution ActiveSearcher::mutate(const Solution& src, float rate, unsigned seed) {
    std::mt19937 lrng(seed);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_int_distribution<int>    actD(0, 3);
    std::uniform_int_distribution<int>    lenD(-5, 5);

    Solution result = src;

    // Точечные мутации
    for (int i = 0; i < (int)result.actions.size(); i++)
        if (prob(lrng) < rate)
            result.actions[i] = static_cast<Action>(actD(lrng));

    // Вставка/удаление
    if (prob(lrng) < 0.3f) {
        int delta = lenD(lrng);
        if (delta > 0) {
            for (int d = 0; d < delta; d++)
                result.actions.push_back(static_cast<Action>(actD(lrng)));
        } else if (delta < 0 && (int)result.actions.size() > -delta) {
            result.actions.resize(result.actions.size() + delta);
        }
    }

    // Инверсия фрагмента
    if (prob(lrng) < 0.2f && result.actions.size() > 4) {
        std::uniform_int_distribution<int> posD(0, (int)result.actions.size() - 2);
        int a = posD(lrng);
        int b = a + 1 + (posD(lrng) % (int)(result.actions.size() - a - 1));
        std::reverse(result.actions.begin() + a, result.actions.begin() + b);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Кросс-брид двух решений (одноточечное)
// ---------------------------------------------------------------------------
Solution ActiveSearcher::crossover(const Solution& s1, const Solution& s2,
                                    unsigned seed) {
    std::mt19937 lrng(seed);

    if (s1.actions.empty()) return s2;
    if (s2.actions.empty()) return s1;

    // Выбираем точку разреза
    std::uniform_int_distribution<int> d1(0, (int)s1.actions.size() - 1);
    std::uniform_int_distribution<int> d2(0, (int)s2.actions.size() - 1);
    int cut1 = d1(lrng);
    int cut2 = d2(lrng);

    Solution result;
    result.actions.insert(result.actions.end(),
                          s1.actions.begin(), s1.actions.begin() + cut1);
    result.actions.insert(result.actions.end(),
                          s2.actions.begin() + cut2, s2.actions.end());

    // Обрезаем до разумной длины
    if ((int)result.actions.size() > RSADConfig::MAX_SOL_LEN)
        result.actions.resize(RSADConfig::MAX_SOL_LEN);

    return result;
}

// ---------------------------------------------------------------------------
// Случайное блуждание
// ---------------------------------------------------------------------------
Solution ActiveSearcher::randomWalk(const Problem& problem, unsigned seed, int maxLen) {
    std::mt19937 lrng(seed);
    std::uniform_int_distribution<int> actD(0, 3);

    Solution sol;
    int x = problem.startX, y = problem.startY;

    for (int step = 0; step < maxLen; step++) {
        if (x == problem.goalX && y == problem.goalY) break;

        // Случайное направление
        int act = actD(lrng);
        int nx = x, ny = y;
        switch (act) {
            case 0: ny--; break; case 1: ny++; break;
            case 2: nx--; break; case 3: nx++; break;
        }

        if (!problem.isObstacle(nx, ny)) {
            sol.actions.push_back(static_cast<Action>(act));
            x = nx; y = ny;
        }
    }
    return sol;
}

// ---------------------------------------------------------------------------
// Жадное блуждание с шумом (greedy + ε-случайность)
// ---------------------------------------------------------------------------
Solution ActiveSearcher::greedyWalk(const Problem& problem, unsigned seed,
                                      float noiseLevel) {
    std::mt19937 lrng(seed);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_int_distribution<int>    actD(0, 3);

    Solution sol;
    int x = problem.startX, y = problem.startY;
    const int G = problem.gridSize;

    for (int step = 0; step < RSADConfig::MAX_SOL_LEN; step++) {
        if (x == problem.goalX && y == problem.goalY) break;

        int bestAct = -1;
        float bestDist = 1e9f;

        // ε-жадный выбор
        if (prob(lrng) < noiseLevel) {
            // Случайный ход
            for (int attempt = 0; attempt < 4; attempt++) {
                int act = actD(lrng);
                int nx = x, ny = y;
                switch (act) {
                    case 0: ny--; break; case 1: ny++; break;
                    case 2: nx--; break; case 3: nx++; break;
                }
                if (!problem.isObstacle(nx, ny)) { bestAct = act; break; }
            }
        } else {
            // Жадный: выбираем направление, максимально приближающее к цели
            for (int act = 0; act < 4; act++) {
                int nx = x, ny = y;
                switch (act) {
                    case 0: ny--; break; case 1: ny++; break;
                    case 2: nx--; break; case 3: nx++; break;
                }
                if (!problem.isObstacle(nx, ny)) {
                    float d = sqrtf((float)((nx-problem.goalX)*(nx-problem.goalX)
                                          + (ny-problem.goalY)*(ny-problem.goalY)));
                    if (d < bestDist) { bestDist = d; bestAct = act; }
                }
            }
        }

        if (bestAct < 0) break;
        sol.actions.push_back(static_cast<Action>(bestAct));
        switch (bestAct) {
            case 0: y--; break; case 1: y++; break;
            case 2: x--; break; case 3: x++; break;
        }
    }
    return sol;
}

// ---------------------------------------------------------------------------
// Планирование запросов (оценка ΔK)
// ---------------------------------------------------------------------------
std::vector<SearchQuery> ActiveSearcher::planQueries(
    const std::vector<Solution>& pool,
    const Problem& problem,
    int numQueries)
{
    std::vector<SearchQuery> queries;
    int N = static_cast<int>(pool.size());

    std::uniform_int_distribution<int> poolIdx(0, std::max(0, N-1));
    std::uniform_int_distribution<int> seedD(0, 999999);

    for (int i = 0; i < numQueries; i++) {
        SearchQuery q;
        q.seed = seedD(rng_);

        // Выбираем тип запроса
        int typeChoice = i % 5;
        switch(typeChoice) {
            case 0: q.type = QueryType::MUTATION;     q.cost = 0.1f; break;
            case 1: q.type = QueryType::CROSSOVER;    q.cost = 0.15f; break;
            case 2: q.type = QueryType::RANDOM_WALK;  q.cost = 0.05f; break;
            case 3: q.type = QueryType::GRAPH_EXPLORE;q.cost = 0.2f; break;
            case 4: q.type = QueryType::LLM_QUERY;    q.cost = 0.8f; break;
        }

        if (N > 0) {
            q.parentIdx1 = poolIdx(rng_) % N;
            q.parentIdx2 = poolIdx(rng_) % N;
        }

        q.expectedDeltaK = estimateDeltaK(q, pool, problem);
        q.utility = q.expectedDeltaK - lambda_ * q.cost;
        queries.push_back(q);
    }

    // Сортируем по полезности
    std::sort(queries.begin(), queries.end(),
              [](auto& a, auto& b){ return a.utility > b.utility; });

    return queries;
}

// ---------------------------------------------------------------------------
// Оценка ожидаемого прироста K_eff
// ---------------------------------------------------------------------------
float ActiveSearcher::estimateDeltaK(const SearchQuery& q,
                                      const std::vector<Solution>& pool,
                                      const Problem& problem) {
    // Упрощённая эвристика:
    // - Мутация: ΔK ~ разнообразие мутаций × вероятность получения нового
    // - Crossover: зависит от разности родителей
    // - Random: высокая вероятность нового, но может быть плохим
    // - LLM: высокий потенциал, но и высокая стоимость

    float kEff = (float)Utils::computeKeff(pool, RSADConfig::DELTA_DIST);
    float kTarget = (float)RSADConfig::K_MIN;

    float gap = kTarget - kEff;
    if (gap <= 0) return 0.0f;

    switch(q.type) {
        case QueryType::MUTATION: {
            if (q.parentIdx1 < 0 || (int)pool.size() == 0) return 0.5f;
            // ΔK тем выше, чем разнообразнее уже есть решение-родитель
            float parentScore = pool[q.parentIdx1].totalScore;
            return 1.0f * (1.0f - parentScore * 0.3f);
        }
        case QueryType::CROSSOVER: {
            if (q.parentIdx1 < 0 || q.parentIdx2 < 0) return 0.3f;
            if ((int)pool.size() < 2) return 0.3f;
            // ΔK зависит от расстояния между родителями
            float dist = Utils::editDistance(pool[q.parentIdx1], pool[q.parentIdx2]);
            return std::min(2.0f, dist / 10.0f);
        }
        case QueryType::RANDOM_WALK:
            return 0.8f;  // Всегда генерирует что-то новое
        case QueryType::GRAPH_EXPLORE:
            return 1.2f;
        case QueryType::LLM_QUERY: {
            // ΔK LLM: зависит от текущей нехватки разнообразия
            // Чем меньше K_eff относительно K_min, тем ценнее LLM
            float kEff = (float)Utils::computeKeff(pool, RSADConfig::DELTA_DIST);
            float kMin = (float)RSADConfig::K_MIN;
            float deficit = std::max(0.0f, kMin - kEff);
            // LLM генерирует семантически разные решения →
            // ожидаем добавить ~30% бюджета в K_eff
            return 1.5f + deficit * 0.4f;
        }
        default:
            return 0.5f;
    }
}

// ---------------------------------------------------------------------------
// Выполнение запроса
// ---------------------------------------------------------------------------
std::vector<Solution> ActiveSearcher::executeQuery(
    const SearchQuery& q,
    const std::vector<Solution>& pool,
    const Problem& problem,
    int batchSize)
{
    std::vector<Solution> newSols;

    switch(q.type) {
        case QueryType::MUTATION: {
            if (pool.empty()) break;
            const Solution& src = pool[q.parentIdx1 >= 0 ?
                                        q.parentIdx1 % (int)pool.size() : 0];
            for (int b = 0; b < batchSize; b++) {
                float rate = 0.05f + 0.05f * (b % 5);
                newSols.push_back(mutate(src, rate, q.seed + b));
            }
            break;
        }
        case QueryType::CROSSOVER: {
            if ((int)pool.size() < 2) break;
            const Solution& p1 = pool[q.parentIdx1 % (int)pool.size()];
            const Solution& p2 = pool[q.parentIdx2 % (int)pool.size()];
            for (int b = 0; b < batchSize; b++)
                newSols.push_back(crossover(p1, p2, q.seed + b));
            break;
        }
        case QueryType::RANDOM_WALK: {
            for (int b = 0; b < batchSize; b++)
                newSols.push_back(randomWalk(problem, q.seed + b));
            break;
        }
        case QueryType::GRAPH_EXPLORE: {
            // Жадные блуждания с разными уровнями шума
            for (int b = 0; b < batchSize; b++) {
                float noise = 0.1f + 0.1f * (b % 5);
                newSols.push_back(greedyWalk(problem, q.seed + b, noise));
            }
            break;
        }
        case QueryType::LLM_QUERY: {
            // ── Настоящий LLM-советник: анализирует пул и генерирует
            //    решения через карту неудач + prompted search + реальный LLM ──
            RSAD_LOG("[ActiveSearcher] LLM query → LLMAdvisor (pool=%zu)\n",
                     pool.size());
            auto advised = advisor_.advise(pool, problem, batchSize, q.seed);
            for (auto& s : advised) {
                if (!s.actions.empty()) newSols.push_back(s);
            }
            // Добираем если LLMAdvisor вернул меньше чем batchSize
            while ((int)newSols.size() < batchSize) {
                newSols.push_back(greedyWalk(problem, q.seed + (unsigned)newSols.size(), 0.2f));
            }
            break;
        }
    }

    return newSols;
}

// ---------------------------------------------------------------------------
// Главный метод: расширение пула до K_min
// ---------------------------------------------------------------------------
std::vector<Solution> ActiveSearcher::search(
    const std::vector<Solution>& initialPool,
    const Problem& problem,
    int kMin,
    int maxIterations)
{
    std::vector<Solution> pool = initialPool;
    std::vector<Solution> allNew;

    RSAD_LOG("[ActiveSearcher] Starting active search (K_eff target=%d, max_iter=%d)\n",
           kMin, maxIterations);

    for (int iter = 0; iter < maxIterations; iter++) {
        int kEff = Utils::computeKeff(pool, RSADConfig::DELTA_DIST);
        float H  = Utils::computeEntropy(pool);

        RSAD_LOG("[ActiveSearcher] Iter %d: K_eff=%d (target=%d), H=%.3f, pool=%zu\n",
               iter, kEff, kMin, H, pool.size());

        if (kEff >= kMin) {
            RSAD_LOG("[ActiveSearcher] K_eff target reached!\n");
            break;
        }

        // Планируем запросы
        auto queries = planQueries(pool, problem, 15);

        RSAD_LOG("[ActiveSearcher]   Best query: type=%d, utility=%.2f, ΔK=%.2f\n",
               (int)queries[0].type,
               queries[0].utility,
               queries[0].expectedDeltaK);

        // Выполняем топ-3 запроса
        int executed = 0;
        for (auto& q : queries) {
            if (executed >= 3) break;
            if (q.utility <= 0) continue;

            auto newSols = executeQuery(q, pool, problem, 8);
            for (auto& s : newSols) {
                if (!s.actions.empty()) {
                    pool.push_back(s);
                    allNew.push_back(s);
                }
            }
            executed++;
        }

        // Ограничиваем размер пула (убираем дубликаты)
        if ((int)pool.size() > kMin * 20) {
            // Оставляем лучшие по totalScore
            std::sort(pool.begin(), pool.end(),
                      [](auto& a, auto& b){ return a.totalScore > b.totalScore; });
            pool.resize(kMin * 15);
        }
    }

    int finalKEff = Utils::computeKeff(pool, RSADConfig::DELTA_DIST);
    RSAD_LOG("[ActiveSearcher] Final K_eff=%d, total new solutions=%zu\n",
           finalKEff, allNew.size());

    return allNew;
}

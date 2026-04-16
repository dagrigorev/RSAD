// =============================================================================
// RSADAgent_cpu.cpp — Реализация главного цикла агента РСАД
// =============================================================================
#include "RSADAgent.h"
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <numeric>

// Условный printf (только при verbose_=true)
#define VLOG(...) do { if (verbose_) printf(__VA_ARGS__); } while(0)

// =============================================================================
#include "RSADAgent.h"
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <numeric>

// ---------------------------------------------------------------------------
// AgentStats::print
// ---------------------------------------------------------------------------
void AgentStats::print() const {
    printf("\n=== RSAD Agent Statistics ===\n");
    printf("  Status:          %s\n",    found ? "SOLUTION FOUND" : "NOT FOUND");
    printf("  Iterations:      %d\n",    iterations);
    printf("  Total solutions: %d\n",    totalSolutions);
    printf("  Valid solutions: %d\n",    validSolutions);
    printf("  Final K_eff:     %d\n",    finalKEff);
    printf("  Best score:      %.4f\n",  bestScore);
    printf("  Time:            %.2fs\n", timeSeconds);
    printf("=============================\n\n");
}

// ---------------------------------------------------------------------------
// Конструктор
// ---------------------------------------------------------------------------
RSADAgent::RSADAgent()
    : memory_(std::make_unique<HypergraphMemory>()),
      evaluator_(std::make_unique<Evaluator>()),
      modeC_(std::make_unique<ModeC>())
{}


void RSADAgent::initProblemBank() {
    if (!problemBank_) {
        VLOG("[Agent] Initializing ProblemBank (30 diverse problems for e2)...\n");
        problemBank_ = std::make_shared<ProblemBank>();
        evaluator_->setProblemBank(problemBank_.get());
        VLOG("[Agent] ProblemBank ready: %zu problems\n", problemBank_->size());
    }
}

void RSADAgent::setLLMModelPath(const std::string& path) {
    llmModelPath_ = path;
    if (!path.empty()) {
        modeC_->setModelPath(path);
        if (modeC_->loadModel()) {
            // Передаём LLM callback в ActiveSearcher
            // (searcher создаётся в solve(), поэтому флаг сохраняем)
            VLOG("[Agent] LLM loaded, will connect to ActiveSearcher\n");
        }
    }
}

void RSADAgent::setAlphas(float a1, float a2, float a3, float a4) {
    evaluator_->setAlphas(a1, a2, a3, a4);
}

void RSADAgent::setKMin(int k) { kMin_ = k; }

// ---------------------------------------------------------------------------
// Запуск трёх режимов
// ---------------------------------------------------------------------------
std::vector<Solution> RSADAgent::runAllModes(
    const Problem& problem,
    const WorldModel& world,
    ModeA& modeA, ModeB& modeB)
{
    std::vector<Solution> pool;

    VLOG("\n--- Mode A: Combinatorial Analogy ---\n");
    auto poolA = modeA.generate(problem, 5);
    pool.insert(pool.end(), poolA.begin(), poolA.end());
    VLOG("Mode A: +%zu solutions\n", poolA.size());

    VLOG("\n--- Mode B: Backward Tracing (GPU BFS) ---\n");
    auto poolB = modeB.generate(problem, 10);
    pool.insert(pool.end(), poolB.begin(), poolB.end());
    VLOG("Mode B: +%zu solutions\n", poolB.size());

    VLOG("\n--- Mode C: Metaphorical Thinking ---\n");
    auto poolC = modeC_->generate(problem, 3);
    pool.insert(pool.end(), poolC.begin(), poolC.end());
    VLOG("Mode C: +%zu solutions\n", poolC.size());

    return pool;
}

// ---------------------------------------------------------------------------
// Оценка пула
// ---------------------------------------------------------------------------
void RSADAgent::evaluateAndFilter(
    std::vector<Solution>& pool,
    const std::vector<SimResult>& sims,
    const Problem& problem)
{
    assert(pool.size() == sims.size());
    evaluator_->evaluate(pool, sims, problem);

    // Сортировка по убыванию
    std::vector<int> idx(pool.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&](int a, int b){ return pool[a].totalScore > pool[b].totalScore; });

    std::vector<Solution> sorted;
    sorted.reserve(pool.size());
    for (int i : idx) sorted.push_back(pool[i]);
    pool = std::move(sorted);
}

// ---------------------------------------------------------------------------
// Обновление памяти
// ---------------------------------------------------------------------------
void RSADAgent::updateMemory(const Problem& problem, const Solution& best) {
    if (best.actions.empty()) return;

    // Ищем концепции проблемы
    std::vector<uint32_t> probConcs;
    for (auto& c : memory_->getConcepts()) {
        if (c.label.find("grid") != std::string::npos ||
            c.label.find("path") != std::string::npos) {
            probConcs.push_back(c.id);
            if ((int)probConcs.size() >= 3) break;
        }
    }
    if (probConcs.empty()) {
        uint32_t cid = memory_->addConcept("grid_navigation");
        probConcs.push_back(cid);
    }

    memory_->recordSolution(probConcs, best, best.totalScore);
}

// ---------------------------------------------------------------------------
// Главный метод: solve(P)
// ---------------------------------------------------------------------------
Solution RSADAgent::solve(const Problem& problem) {
    double tStart = Utils::nowSeconds();
    stats_ = {};
    bestSolution_ = {};

    VLOG("\n");
    VLOG("╔══════════════════════════════════════════════════════╗\n");
    VLOG("║                  GridWorld %dx%d                     ║\n",
           problem.gridSize, problem.gridSize);
    VLOG("╚══════════════════════════════════════════════════════╝\n");
    VLOG("  Старт: (%d,%d)  Цель: (%d,%d)\n",
           problem.startX, problem.startY,
           problem.goalX, problem.goalY);
    VLOG("  K_min: %d  Порог: %.2f\n\n", kMin_, RSADConfig::THRESHOLD_TOTAL);

    // Загрузка демо-базы знаний
    memory_->loadDemoKnowledge(problem);

    // Создаём WorldModel и Mode-объекты под эту задачу
    WorldModel world(problem);
    ModeA modeA(*memory_, world);
    ModeB modeB(world);
    ActiveSearcher searcher(world, *memory_);
    // Подключаем LLM callback если модель загружена
    if (modeC_ && !llmModelPath_.empty()) {
        searcher.setLLMCallback([this](const std::string& prompt) {
            return modeC_->queryLLM(prompt);
        });
    }
    // Инициализируем банк задач для истинной e2 (однократно)
    initProblemBank();

    // ---- Шаг 1: инициализация пула ----
    std::vector<Solution> pool;

    // ---- Шаг 2: три режима мышления ----
    VLOG("=== Step 2: Generating candidate solutions ===\n");
    auto candidates = runAllModes(problem, world, modeA, modeB);
    pool.insert(pool.end(), candidates.begin(), candidates.end());

    // ---- Итерационный цикл ----
    int maxOuterIter = RSADConfig::MAX_ITER_SEARCH + 1;
    for (int outerIter = 0; outerIter < maxOuterIter; outerIter++) {
        stats_.iterations = outerIter + 1;

        if (pool.empty()) {
            VLOG("[Agent] Pool is empty, generating random solutions...\n");
            for (int i = 0; i < 20; i++) {
                std::mt19937 rng(i * 7 + 13);
                std::uniform_int_distribution<int> actD(0, 3);
                Solution s;
                int steps = problem.gridSize * 3;
                int x = problem.startX, y = problem.startY;
                for (int step = 0; step < steps; step++) {
                    int act = actD(rng);
                    int nx=x, ny=y;
                    switch(act){case 0:ny--;break;case 1:ny++;break;case 2:nx--;break;case 3:nx++;break;}
                    if(!problem.isObstacle(nx,ny)){s.actions.push_back((Action)act);x=nx;y=ny;}
                    if(x==problem.goalX&&y==problem.goalY)break;
                }
                pool.push_back(s);
            }
        }

        VLOG("\n=== Iteration %d: Simulating %zu solutions (GPU) ===\n",
               outerIter, pool.size());

        // ---- Шаг 3: симуляция и оценка ----
        auto sims = world.simulateBatch(pool);
        evaluateAndFilter(pool, sims, problem);

        // Статистика
        stats_.totalSolutions = (int)pool.size();
        stats_.validSolutions = 0;
        for (auto& s : pool) if (s.valid) stats_.validSolutions++;

        VLOG("  Valid: %d / %d\n", stats_.validSolutions, stats_.totalSolutions);

        // Обновляем лучшее решение
        if (!pool.empty() && pool[0].totalScore > bestSolution_.totalScore) {
            bestSolution_ = pool[0];
            VLOG("  Best score: %.4f (e1=%.2f e2=%.2f e3=%.2f e4=%.2f) len=%zu\n",
                   bestSolution_.totalScore,
                   bestSolution_.e1, bestSolution_.e2,
                   bestSolution_.e3, bestSolution_.e4,
                   bestSolution_.actions.size());
        }

        // ---- Шаг 4: вычисление K_eff ----
        int kEff = Utils::computeKeff(pool, RSADConfig::DELTA_DIST);
        float H  = Utils::computeEntropy(pool);
        VLOG("  K_eff=%d (min=%d)  H=%.3f\n", kEff, kMin_, H);

        stats_.finalKEff = kEff;

        // Проверяем условие успеха
        if (bestSolution_.valid) {
            stats_.found = true;
            VLOG("\n  ✓ Приемлемое решение найдено (score=%.4f >= %.2f)\n",
                   bestSolution_.totalScore, RSADConfig::THRESHOLD_TOTAL);
        }

        // ---- Шаг 5: активный поиск если K_eff < K_min ИЛИ H < H_min ----
        bool needSearch = (kEff < kMin_) || (H < RSADConfig::H_MIN && !stats_.found);
        if (needSearch) {
            if (kEff < kMin_)
                VLOG("\n=== Active Search: K_eff=%d < K_min=%d ===\n", kEff, kMin_);
            else
                VLOG("\n=== Active Search: H=%.3f < H_min=%.2f (low diversity) ===\n",
                     H, RSADConfig::H_MIN);
        }
        if (needSearch) {
            VLOG("\n=== Active Search: K_eff=%d < K_min=%d ===\n", kEff, kMin_);
            auto newSols = searcher.search(pool, problem, kMin_, 3);
            pool.insert(pool.end(), newSols.begin(), newSols.end());
            // Повторяем симуляцию новых решений
            if (!newSols.empty()) {
                auto newSims = world.simulateBatch(newSols);
                // Добавляем SimResults к обновлённому пулу
                // (уже добавлено в pool выше, поэтому только обновляем оценки)
                std::vector<SimResult> allSims = sims;
                allSims.insert(allSims.end(), newSims.begin(), newSims.end());
                evaluateAndFilter(pool, allSims, problem);
                if (!pool.empty() && pool[0].totalScore > bestSolution_.totalScore) {
                    bestSolution_ = pool[0];
                }
            }
        } else if (stats_.found) {
            break;  // K_eff достигнут, решение найдено
        }

        // Если уже достигли хорошего результата, выходим
        if (stats_.found && outerIter >= 2) break;
    }

    // ---- Шаг 6: выбор лучшего ----
    if (bestSolution_.actions.empty() && !pool.empty()) {
        bestSolution_ = pool[0];
        stats_.found = (bestSolution_.totalScore >= RSADConfig::THRESHOLD_TOTAL);
    }

    // ---- Шаг 7: обновление памяти ----
    if (!bestSolution_.actions.empty()) {
        updateMemory(problem, bestSolution_);
    }

    stats_.bestScore   = bestSolution_.totalScore;
    stats_.timeSeconds = Utils::nowSeconds() - tStart;

    return bestSolution_;
}

// ---------------------------------------------------------------------------
// Визуализация
// ---------------------------------------------------------------------------
void RSADAgent::printBestSolution(const Problem& problem) const {
    if (bestSolution_.actions.empty()) {
        VLOG("[Agent] No solution to display\n");
        return;
    }
    WorldModel tmpWorld(problem);
    tmpWorld.printPath(bestSolution_);
}

// =============================================================================
// Modes.cu — Реализация трёх режимов мышления РСАД
// =============================================================================
#include "Modes.h"
#include "llamacpp_impl.h"
#include <algorithm>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <sstream>

// Условная компиляция llama.cpp
#ifdef RSAD_USE_LLAMACPP

#endif

// =============================================================================
// ========  РЕЖИМ A: Комбинаторная аналогия  ==================================
// =============================================================================

ModeA::ModeA(HypergraphMemory& memory, const WorldModel& world)
    : memory_(memory), world_(world) {}

std::vector<uint32_t> ModeA::extractProblemConcepts(const Problem& problem) {
    // Для GridWorld: ищем концепции, соответствующие навигации
    // В реальной системе это делается через NLP-парсинг описания проблемы
    std::vector<uint32_t> ids;
    const auto& concepts = memory_.getConcepts();
    for (auto& c : concepts) {
        // Берём концепции, связанные с навигацией
        if (c.label.find("grid") != std::string::npos ||
            c.label.find("path") != std::string::npos ||
            c.label.find("nav")  != std::string::npos ||
            c.label.find("maze") != std::string::npos) {
            ids.push_back(c.id);
        }
    }
    // Ограничиваем до 5 концепций
    if (ids.size() > 5) ids.resize(5);
    return ids;
}

Solution ModeA::combine(const Solution& s1, const Solution& s2,
                         const Problem& problem) {
    // Оператор ⊕: "шахматное" чередование действий из двух решений
    // Выбираем из каждого лучшее с точки зрения приближения к цели
    Solution result;
    int g = problem.gridSize - 1;

    int x = problem.startX, y = problem.startY;
    size_t i1 = 0, i2 = 0;
    int maxSteps = RSADConfig::MAX_SOL_LEN;

    while ((x != problem.goalX || y != problem.goalY) && maxSteps-- > 0) {
        // Выбираем действие, которое больше приближает к цели
        Action chosen = Action::NONE;
        float bestDist = 1e9f;

        auto tryActions = [&](const std::vector<Action>& actions, size_t& idx) {
            if (idx < actions.size()) {
                Action a = actions[idx];
                int nx = x, ny = y;
                switch (a) {
                    case Action::UP:    ny--; break;
                    case Action::DOWN:  ny++; break;
                    case Action::LEFT:  nx--; break;
                    case Action::RIGHT: nx++; break;
                    default: break;
                }
                if (!problem.isObstacle(nx, ny)) {
                    float dist = sqrtf((float)((nx - problem.goalX)*(nx - problem.goalX)
                                             + (ny - problem.goalY)*(ny - problem.goalY)));
                    if (dist < bestDist) {
                        bestDist = dist;
                        chosen = a;
                    }
                }
                idx++;
            }
        };

        tryActions(s1.actions, i1);
        tryActions(s2.actions, i2);

        if (chosen == Action::NONE) break;

        result.actions.push_back(chosen);
        switch (chosen) {
            case Action::UP:    y--; break;
            case Action::DOWN:  y++; break;
            case Action::LEFT:  x--; break;
            case Action::RIGHT: x++; break;
            default: break;
        }
    }
    return result;
}

std::vector<Solution> ModeA::generate(const Problem& problem, int kA) {
    RSAD_LOG("[ModeA] Searching hypergraph for structural analogies...\n");

    std::vector<uint32_t> problemConcepts = extractProblemConcepts(problem);
    if (problemConcepts.empty()) {
        RSAD_LOG("[ModeA] No matching concepts found, using all edges\n");
        // Берём первые концепции
        auto& concepts = memory_.getConcepts();
        for (auto& c : concepts) { problemConcepts.push_back(c.id); if ((int)problemConcepts.size() >= 5) break; }
    }

    // Находим топ-kA похожих гиперрёбер
    auto similarEdges = memory_.findSimilarEdges(problemConcepts, kA * 2);
    RSAD_LOG("[ModeA] Found %zu similar edges (requested top-%d)\n", similarEdges.size(), kA);

    std::vector<Solution> pool;

    // Собираем решения из похожих гиперрёбер
    std::vector<const Solution*> candidateSols;
    for (auto& [edgeId, sim] : similarEdges) {
        if (sim < RSADConfig::THRESHOLD_SIM) continue;
        const auto& edges = memory_.getEdges();
        if (edgeId < edges.size() && edges[edgeId].hasSolution) {
            pool.push_back(edges[edgeId].solution);
            candidateSols.push_back(&edges[edgeId].solution);
            RSAD_LOG("[ModeA]   Edge %u (sim=%.2f): %s\n",
                   edgeId, sim, edges[edgeId].label.c_str());
        }
    }

    // Комбинируем пары решений с помощью оператора ⊕
    int numCands = static_cast<int>(candidateSols.size());
    for (int i = 0; i < numCands; ++i)
        for (int j = i + 1; j < numCands; ++j) {
            Solution combined = combine(*candidateSols[i], *candidateSols[j], problem);
            if (!combined.actions.empty())
                pool.push_back(combined);
        }

    RSAD_LOG("[ModeA] Generated %zu candidate solutions\n", pool.size());
    return pool;
}

// =============================================================================
// ========  РЕЖИМ B: Обратный ход  ============================================
// =============================================================================

ModeB::ModeB(const WorldModel& world) : world_(world), chainer_(world) {}

Solution ModeB::perturbPath(const std::vector<Action>& base,
                              const Problem& problem, unsigned seed) {
    std::mt19937 rng(seed);
    Solution s;
    s.actions = base;

    if (s.actions.empty()) return s;

    std::uniform_int_distribution<int> idxDist(0, (int)s.actions.size() - 1);
    std::uniform_int_distribution<int> actDist(0, 3);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);

    // Случайные мутации (10..30% действий)
    float mutRate = 0.10f + 0.05f * (seed % 5);
    for (int i = 0; i < (int)s.actions.size(); ++i)
        if (prob(rng) < mutRate)
            s.actions[i] = static_cast<Action>(actDist(rng));

    // Иногда добавляем дополнительные "обходные" действия
    if (prob(rng) < 0.3f && (int)s.actions.size() < RSADConfig::MAX_SOL_LEN - 4) {
        int insertPos = idxDist(rng);
        Action extra1 = static_cast<Action>(actDist(rng));
        // Добавляем и "откатываем" — чтобы не потерять путь
        s.actions.insert(s.actions.begin() + insertPos, extra1);
    }

    return s;
}

std::vector<Solution> ModeB::generate(const Problem& problem, int numPaths) {
    RSAD_LOG("[ModeB] True backward tracing via M^{-1} decomposition\n");

    std::vector<Solution> pool;

    // ── Шаг 1: Настоящий обратный ход через M^{-1} ───────────────────────────
    // BackwardChainer решает каждый отрезок пути через обратный граф переходов,
    // что принципиально отличается от прямого BFS:
    //   - Декомпозиция на подзадачи с промежуточными целями
    //   - 5 разных стратегий выбора waypoints
    //   - Каждый отрезок решается независимо через M^{-1}(Q, S) = predecessor
    auto chainerSols = chainer_.generate(problem, numPaths - 2);
    for (auto& s : chainerSols) {
        if (!s.actions.empty()) pool.push_back(s);
    }
    RSAD_LOG("[ModeB] BackwardChainer: %zu solutions\n", pool.size());

    // ── Шаг 2: Прямой BFS как опорная линия (для сравнения и добавления) ──
    auto basePath = world_.bfsGPU(problem.startX, problem.startY,
                                   problem.goalX,  problem.goalY);
    if (!basePath.empty()) {
        Solution base; base.actions = basePath;
        pool.push_back(base);
        RSAD_LOG("[ModeB] BFS baseline: len=%zu\n", basePath.size());
    } else {
        RSAD_LOG("[ModeB] WARNING: No direct BFS path found\n");
    }

    RSAD_LOG("[ModeB] Generated %zu candidate solutions (backward trace + BFS)\n", pool.size());
    return pool;
}

// =============================================================================
// ========  РЕЖИМ C: Образное мышление  =======================================
// =============================================================================

ModeC::ModeC() { initMetaphors(); }

void ModeC::initMetaphors() {
    // --- Метафора 1: Водный поток ---
    // Принцип: вода течёт по пути наименьшего сопротивления, всегда вниз
    {
        NatureMetaphor m;
        m.name = "Water Flow";
        m.description = "Least-resistance path";
        // Смещение к цели: предпочитаем RIGHT и DOWN (если цель вправо-вниз)
        m.dirBias[0] = 0.15f; // UP
        m.dirBias[1] = 0.35f; // DOWN
        m.dirBias[2] = 0.15f; // LEFT
        m.dirBias[3] = 0.35f; // RIGHT
        // Инициализация эмбеддинга
        std::mt19937 rng(111);
        std::normal_distribution<float> nd(0,1);
        float norm = 0;
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) {
            m.embedding[i] = nd(rng); norm += m.embedding[i]*m.embedding[i];
        }
        norm = sqrtf(norm);
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) m.embedding[i] /= norm;
        metaphors_.push_back(m);
    }

    // --- Метафора 2: Муравьиная тропа ---
    // Принцип: феромонные метки усиливают оптимальные маршруты
    {
        NatureMetaphor m;
        m.name = "Ant Colony";
        m.description = "ACO: pheromone trails";
        m.dirBias[0] = 0.25f;
        m.dirBias[1] = 0.30f;
        m.dirBias[2] = 0.15f;
        m.dirBias[3] = 0.30f;
        std::mt19937 rng(222);
        std::normal_distribution<float> nd(0,1);
        float norm = 0;
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) {
            m.embedding[i] = nd(rng); norm += m.embedding[i]*m.embedding[i];
        }
        norm = sqrtf(norm);
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) m.embedding[i] /= norm;
        metaphors_.push_back(m);
    }

    // --- Метафора 3: Световой луч (преломление) ---
    // Принцип: кратчайший путь в пространстве с "показателем преломления"
    {
        NatureMetaphor m;
        m.name = "Light Ray";
        m.description = "Fermat: shortest optical path";
        m.dirBias[0] = 0.20f;
        m.dirBias[1] = 0.30f;
        m.dirBias[2] = 0.20f;
        m.dirBias[3] = 0.30f;
        std::mt19937 rng(333);
        std::normal_distribution<float> nd(0,1);
        float norm = 0;
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) {
            m.embedding[i] = nd(rng); norm += m.embedding[i]*m.embedding[i];
        }
        norm = sqrtf(norm);
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) m.embedding[i] /= norm;
        metaphors_.push_back(m);
    }

    // --- Метафора 4: Нейронные сети (обратное распространение) ---
    {
        NatureMetaphor m;
        m.name = "Neural Net";
        m.description = "Gradient descent to target";
        m.dirBias[0] = 0.25f; m.dirBias[1] = 0.25f;
        m.dirBias[2] = 0.25f; m.dirBias[3] = 0.25f;
        std::mt19937 rng(444);
        std::normal_distribution<float> nd(0,1);
        float norm = 0;
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) {
            m.embedding[i] = nd(rng); norm += m.embedding[i]*m.embedding[i];
        }
        norm = sqrtf(norm);
        for (int i = 0; i < RSADConfig::EMBED_DIM; i++) m.embedding[i] /= norm;
        metaphors_.push_back(m);
    }

    RSAD_LOG("[ModeC] Initialized %zu nature metaphors\n", metaphors_.size());
}

float ModeC::cosSimilarity(const float* a, const float* b, int dim) {
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < dim; i++) { dot+=a[i]*b[i]; na+=a[i]*a[i]; nb+=b[i]*b[i]; }
    return (na*nb > 1e-9f) ? dot/sqrtf(na*nb) : 0.0f;
}

void ModeC::embedProblem(const Problem& problem, float* out) {
    // Простой детерминированный эмбеддинг по характеристикам проблемы
    uint32_t seed = (problem.gridSize * 13) ^ (problem.goalX * 7919) ^ problem.goalY;
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0,1);
    float norm = 0;
    for (int i = 0; i < RSADConfig::EMBED_DIM; i++) {
        out[i] = nd(rng); norm += out[i]*out[i];
    }
    norm = sqrtf(norm);
    for (int i = 0; i < RSADConfig::EMBED_DIM; i++) out[i] /= norm;
}

Solution ModeC::transformMetaphorToSolution(const NatureMetaphor& m,
                                              const Problem& problem,
                                              unsigned seed)
{
    // Генерируем решение, следуя смещению метафоры,
    // но адаптируясь к реальному положению цели
    Solution sol;
    std::mt19937 rng(seed);

    int x = problem.startX, y = problem.startY;
    int G = problem.gridSize;
    int maxSteps = RSADConfig::MAX_SOL_LEN;

    // Адаптируем смещение: если цель правее, усиливаем RIGHT
    float bias[4];
    std::copy(m.dirBias, m.dirBias + 4, bias);

    float dx = (float)(problem.goalX - problem.startX);
    float dy = (float)(problem.goalY - problem.startY);
    float scale = 0.3f;

    if (dx > 0) bias[3] += scale * dx / G;  // RIGHT
    if (dx < 0) bias[2] -= scale * dx / G;  // LEFT
    if (dy > 0) bias[1] += scale * dy / G;  // DOWN
    if (dy < 0) bias[0] -= scale * dy / G;  // UP

    // Нормализуем
    float total = bias[0]+bias[1]+bias[2]+bias[3];
    for (int i = 0; i < 4; i++) bias[i] /= total;

    // Накопленное распределение
    float cdf[4];
    cdf[0] = bias[0];
    for (int i = 1; i < 4; i++) cdf[i] = cdf[i-1] + bias[i];

    std::uniform_real_distribution<float> uDist(0.0f, 1.0f);

    for (int step = 0; step < maxSteps; step++) {
        if (x == problem.goalX && y == problem.goalY) break;

        // Выбор действия по смещённому распределению
        float r = uDist(rng);
        int actIdx = 3;
        for (int i = 0; i < 4; i++) { if (r <= cdf[i]) { actIdx = i; break; } }

        // Пробуем выбранное действие и случайные обходы при препятствии
        bool moved = false;
        for (int attempt = 0; attempt < 4; attempt++) {
            int tryAct = (actIdx + attempt) % 4;
            int nx = x, ny = y;
            switch (tryAct) {
                case 0: ny--; break;
                case 1: ny++; break;
                case 2: nx--; break;
                case 3: nx++; break;
            }
            if (!problem.isObstacle(nx, ny)) {
                sol.actions.push_back(static_cast<Action>(tryAct));
                x = nx; y = ny;
                moved = true;
                break;
            }
        }
        if (!moved) break;  // Застряли
    }

    return sol;
}

std::vector<Solution> ModeC::generate(const Problem& problem, int mB) {
    RSAD_LOG("[ModeC] Generating solutions via nature metaphors...\n");

    // Вычисляем эмбеддинг проблемы
    float problemEmb[RSADConfig::EMBED_DIM];
    embedProblem(problem, problemEmb);

    // Оцениваем пригодность каждой метафоры
    std::vector<std::pair<int, float>> scored;
    for (int i = 0; i < (int)metaphors_.size(); i++) {
        float fit = cosSimilarity(metaphors_[i].embedding, problemEmb,
                                   RSADConfig::EMBED_DIM);
        scored.emplace_back(i, fit);
        RSAD_LOG("[ModeC]   Metaphor '%s': fit=%.3f\n", metaphors_[i].name.c_str(), fit);
    }

    // Сортируем по пригодности
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b){ return a.second > b.second; });

    int topM = std::min(mB, (int)scored.size());
    std::vector<Solution> pool;

    for (int rank = 0; rank < topM; rank++) {
        const auto& meta = metaphors_[scored[rank].first];
        RSAD_LOG("[ModeC] Using metaphor '%s' (fit=%.3f)\n",
               meta.name.c_str(), scored[rank].second);

        // Генерируем несколько вариантов от каждой метафоры
        for (int v = 0; v < 3; v++) {
            Solution s = transformMetaphorToSolution(meta, problem, 1000 + rank*100 + v);
            if (!s.actions.empty()) pool.push_back(s);
        }

        // Если есть LLM — генерируем через него тоже
        if (modelLoaded_) {
            std::ostringstream prompt;
            prompt << "Ты решаешь задачу навигации в сетке " << problem.gridSize
                   << "x" << problem.gridSize << ". "
                   << "Старт (" << problem.startX << "," << problem.startY << "), "
                   << "Цель (" << problem.goalX << "," << problem.goalY << "). "
                   << "Используя принцип '" << meta.description << "', "
                   << "запиши путь как последовательность U/D/L/R.";
            std::string resp = queryLLM(prompt.str());
            // Парсим ответ LLM → Solution
            Solution llmSol;
            for (char c : resp) {
                char cu = toupper(c);
                if (cu=='U'||cu=='D'||cu=='L'||cu=='R')
                    llmSol.actions.push_back(charToAction(cu));
                if ((int)llmSol.actions.size() >= RSADConfig::MAX_SOL_LEN) break;
            }
            if (!llmSol.actions.empty()) {
                pool.push_back(llmSol);
                RSAD_LOG("[ModeC] LLM solution len=%zu\n", llmSol.actions.size());
            }
        }
    }

    RSAD_LOG("[ModeC] Generated %zu candidate solutions\n", pool.size());
    return pool;
}

// ---------------------------------------------------------------------------
// LLM интеграция (заглушки для CPU-сборки)
// ---------------------------------------------------------------------------
bool ModeC::loadModel() {
#ifdef RSAD_USE_LLAMACPP
    if (modelPath_.empty()) {
        fprintf(stderr, "[ModeC] Model path not set\n");
        return false;
    }
    auto* h = new LlamaHandle();
    if (!h->load(modelPath_, 99)) {
        delete h;
        return false;
    }
    llamaCtx_   = h;
    modelLoaded_ = true;
    return true;
#else
    RSAD_LOG("[ModeC] llama.cpp not compiled in. "
             "Rebuild with -DRSAD_USE_LLAMACPP=ON\n");
    return false;
#endif
}

void ModeC::unloadModel() {
#ifdef RSAD_USE_LLAMACPP
    if (llamaCtx_) {
        delete static_cast<LlamaHandle*>(llamaCtx_);
        llamaCtx_ = nullptr;
    }
#endif
    modelLoaded_ = false;
}

std::string ModeC::queryLLM(const std::string& prompt) {
#ifdef RSAD_USE_LLAMACPP
    if (!modelLoaded_ || !llamaCtx_) return "";
    auto* h = static_cast<LlamaHandle*>(llamaCtx_);
    std::string resp = h->generate(prompt, 200);
    RSAD_LOG("[ModeC] LLM response (%zu chars)\n", resp.size());
    return resp;
#else
    (void)prompt;
    return "";
#endif
}

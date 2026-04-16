// =============================================================================
// HypergraphMemory.cu — Реализация гиперграфовой памяти с GPU ANN
// =============================================================================
#include "HypergraphMemory.h"
#include <cuda_runtime.h>
#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <thrust/sort.h>
#include <thrust/sequence.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <numeric>

// ---------------------------------------------------------------------------
// CUDA ядро: косинусное сходство одного запроса со всеми концепциями
//   query    [EMBED_DIM]
//   embeds   [N x EMBED_DIM] (row-major)
//   sims     [N] — выход
// ---------------------------------------------------------------------------
__global__ void cosineSimilarityKernel(
    const float* __restrict__ query,
    const float* __restrict__ embeds,
    float*       sims,
    int N, int D)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= N) return;

    const float* row = embeds + idx * D;
    float dot = 0.0f, normQ = 0.0f, normE = 0.0f;
    for (int d = 0; d < D; ++d) {
        float q = query[d];
        float e = row[d];
        dot   += q * e;
        normQ += q * q;
        normE += e * e;
    }
    float denom = sqrtf(normQ) * sqrtf(normE);
    sims[idx] = (denom > 1e-9f) ? (dot / denom) : 0.0f;
}

// ---------------------------------------------------------------------------
// Конструктор / Деструктор
// ---------------------------------------------------------------------------
HypergraphMemory::HypergraphMemory() {}

HypergraphMemory::~HypergraphMemory() {
    freeGPUBuffers();
}

void HypergraphMemory::freeGPUBuffers() {
    if (d_embeddings_) {
        cudaFree(d_embeddings_);
        d_embeddings_ = nullptr;
    }
    embeddingCount_ = 0;
}

// ---------------------------------------------------------------------------
// Добавление концепции
// ---------------------------------------------------------------------------
uint32_t HypergraphMemory::addConcept(const std::string& label, const float* embedding) {
    Concept c;
    c.id    = nextConceptId_++;
    c.label = label;
    if (embedding) {
        std::copy(embedding, embedding + RSADConfig::EMBED_DIM, c.embedding);
    } else {
        generateDefaultEmbedding(c.embedding, label);
    }
    concepts_.push_back(std::move(c));
    gpuDirty_ = true;
    return concepts_.back().id;
}

const Concept* HypergraphMemory::getConcept(uint32_t id) const {
    if (id < concepts_.size()) return &concepts_[id];
    return nullptr;
}

void HypergraphMemory::updateConceptImportance(uint32_t id, float delta) {
    if (id < concepts_.size())
        concepts_[id].importance = std::max(0.0f, concepts_[id].importance + delta);
}

// ---------------------------------------------------------------------------
// Гиперрёбра
// ---------------------------------------------------------------------------
uint32_t HypergraphMemory::addHyperedge(const std::vector<uint32_t>& conceptIds,
                                         RelationType rel, float weight,
                                         const std::string& label)
{
    Hyperedge e;
    e.id         = nextEdgeId_++;
    e.conceptIds = conceptIds;
    e.relType    = rel;
    e.weight     = weight;
    e.label      = label;
    edges_.push_back(std::move(e));
    return edges_.back().id;
}

void HypergraphMemory::attachSolution(uint32_t edgeId, const Solution& sol) {
    if (edgeId < edges_.size()) {
        edges_[edgeId].solution    = sol;
        edges_[edgeId].hasSolution = true;
    }
}

void HypergraphMemory::updateEdgeWeight(uint32_t edgeId, float delta) {
    if (edgeId < edges_.size())
        edges_[edgeId].weight = std::max(0.0f, edges_[edgeId].weight + delta);
}

// ---------------------------------------------------------------------------
// GPU: загрузка эмбеддингов
// ---------------------------------------------------------------------------
void HypergraphMemory::uploadEmbeddingsToGPU() {
    if (!gpuDirty_) return;
    freeGPUBuffers();

    int N = static_cast<int>(concepts_.size());
    if (N == 0) return;

    size_t bytes = (size_t)N * RSADConfig::EMBED_DIM * sizeof(float);
    CUDA_CHECK(cudaMalloc(&d_embeddings_, bytes));

    // Копируем построчно
    std::vector<float> hostBuf(N * RSADConfig::EMBED_DIM);
    for (int i = 0; i < N; ++i)
        std::copy(concepts_[i].embedding,
                  concepts_[i].embedding + RSADConfig::EMBED_DIM,
                  hostBuf.data() + i * RSADConfig::EMBED_DIM);

    CUDA_CHECK(cudaMemcpy(d_embeddings_, hostBuf.data(), bytes, cudaMemcpyHostToDevice));
    embeddingCount_ = N;
    gpuDirty_ = false;
}

// ---------------------------------------------------------------------------
// ANN поиск на GPU
// ---------------------------------------------------------------------------
std::vector<ANNResult> HypergraphMemory::findSimilarConcepts(
    const float* queryEmb, int topK) const
{
    const_cast<HypergraphMemory*>(this)->uploadEmbeddingsToGPU();
    int N = static_cast<int>(embeddingCount_);
    if (N == 0) return {};

    // Загружаем запрос на GPU
    float* d_query;
    CUDA_CHECK(cudaMalloc(&d_query, RSADConfig::EMBED_DIM * sizeof(float)));
    CUDA_CHECK(cudaMemcpy(d_query, queryEmb,
                          RSADConfig::EMBED_DIM * sizeof(float),
                          cudaMemcpyHostToDevice));

    // Выделяем буфер схожести
    float* d_sims;
    CUDA_CHECK(cudaMalloc(&d_sims, N * sizeof(float)));

    int threads = RSADConfig::BATCH_THREADS;
    int blocks  = (N + threads - 1) / threads;
    cosineSimilarityKernel<<<blocks, threads>>>(
        d_query, d_embeddings_, d_sims, N, RSADConfig::EMBED_DIM);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK_KERNEL();

    // Копируем обратно на CPU
    std::vector<float> sims(N);
    CUDA_CHECK(cudaMemcpy(sims.data(), d_sims, N * sizeof(float), cudaMemcpyDeviceToHost));

    cudaFree(d_query);
    cudaFree(d_sims);

    // Отбираем топ-K
    std::vector<int> idx(N);
    std::iota(idx.begin(), idx.end(), 0);
    int k = std::min(topK, N);
    std::partial_sort(idx.begin(), idx.begin() + k, idx.end(),
        [&](int a, int b){ return sims[a] > sims[b]; });

    std::vector<ANNResult> results(k);
    for (int i = 0; i < k; ++i) {
        results[i].conceptId  = concepts_[idx[i]].id;
        results[i].similarity = sims[idx[i]];
    }
    return results;
}

// ---------------------------------------------------------------------------
// Структурное сходство гиперребра с набором концепций проблемы (σ)
// ---------------------------------------------------------------------------
float HypergraphMemory::computeStructuralSimilarity(
    uint32_t edgeId, const std::vector<uint32_t>& problemConceptIds) const
{
    if (edgeId >= edges_.size() || problemConceptIds.empty()) return 0.0f;

    const Hyperedge& e = edges_[edgeId];
    if (e.conceptIds.empty()) return 0.0f;

    // Подсчёт пересечения |phi(e) ∩ Concepts(P)| / |Concepts(P)|
    // Здесь phi — простое сравнение ID и эмбеддинговое сходство
    float score = 0.0f;

    // Строим набор концепций ребра
    for (uint32_t pid : problemConceptIds) {
        float bestSim = 0.0f;
        for (uint32_t eid : e.conceptIds) {
            // Точное совпадение
            if (eid == pid) { bestSim = 1.0f; break; }
            // Эмбеддинговое сходство
            if (pid < concepts_.size() && eid < concepts_.size()) {
                const float* a = concepts_[pid].embedding;
                const float* b = concepts_[eid].embedding;
                float dot = 0.0f, na = 0.0f, nb = 0.0f;
                for (int d = 0; d < RSADConfig::EMBED_DIM; ++d) {
                    dot += a[d]*b[d]; na += a[d]*a[d]; nb += b[d]*b[d];
                }
                float sim = (na*nb > 1e-9f) ? dot / sqrtf(na*nb) : 0.0f;
                bestSim = std::max(bestSim, sim);
            }
        }
        score += bestSim;
    }
    return score / static_cast<float>(problemConceptIds.size());
}

// ---------------------------------------------------------------------------
// Поиск похожих гиперрёбер для Mode A
// ---------------------------------------------------------------------------
std::vector<std::pair<uint32_t, float>>
HypergraphMemory::findSimilarEdges(
    const std::vector<uint32_t>& problemConceptIds, int topK) const
{
    std::vector<std::pair<uint32_t, float>> scored;
    scored.reserve(edges_.size());

    for (auto& edge : edges_) {
        float sim = computeStructuralSimilarity(edge.id, problemConceptIds);
        // Взвешиваем по весу ребра
        float weightedSim = sim * edge.weight;
        if (weightedSim > 0.01f)
            scored.emplace_back(edge.id, weightedSim);
    }

    // Сортируем по убыванию
    std::sort(scored.begin(), scored.end(),
        [](auto& a, auto& b){ return a.second > b.second; });

    int k = std::min(topK, static_cast<int>(scored.size()));
    scored.resize(k);
    return scored;
}

// ---------------------------------------------------------------------------
// Запись решения в память (после успешного применения)
// ---------------------------------------------------------------------------
void HypergraphMemory::recordSolution(
    const std::vector<uint32_t>& problemConceptIds,
    const Solution& sol,
    float quality)
{
    if (problemConceptIds.empty()) return;

    // Добавляем концепцию решения
    std::string solLabel = "SOL:" + sol.toString().substr(0, 20);
    uint32_t solConcept  = addConcept(solLabel);

    // Добавляем гиперребро: {проблема} -SOLUTION_OF-> {решение}
    std::vector<uint32_t> ids = problemConceptIds;
    ids.push_back(solConcept);

    uint32_t eid = addHyperedge(ids, RelationType::SOLUTION_OF,
                                 quality, "recorded_solution");
    attachSolution(eid, sol);

    RSAD_LOG("[Memory] Recorded solution (len=%zu, quality=%.2f), total edges: %zu\n",
           sol.actions.size(), quality, edges_.size());
}

// ---------------------------------------------------------------------------
// Вывод состояния памяти
// ---------------------------------------------------------------------------
void HypergraphMemory::print() const {
    RSAD_LOG("=== HypergraphMemory: %zu concepts, %zu hyperedges ===\n",
           concepts_.size(), edges_.size());
    for (auto& e : edges_) {
        RSAD_LOG("  Edge[%u] type=%-14s w=%.2f  concepts=[",
               e.id, relTypeStr(e.relType), e.weight);
        for (size_t i = 0; i < e.conceptIds.size(); ++i) {
            if (i) RSAD_LOG(",");
            if (e.conceptIds[i] < concepts_.size())
                RSAD_LOG("%s", concepts_[e.conceptIds[i]].label.c_str());
        }
        RSAD_LOG("]  sol=%s\n", e.hasSolution ? e.solution.toString().c_str() : "(none)");
    }
}

// ---------------------------------------------------------------------------
// Инициализация демо-базы знаний для GridWorld
// ---------------------------------------------------------------------------
void HypergraphMemory::loadDemoKnowledge(const Problem& problem) {
    RSAD_LOG("[Memory] Loading demo knowledge base for GridWorld %dx%d...\n",
           problem.gridSize, problem.gridSize);

    // Базовые концепции
    uint32_t cGrid     = addConcept("grid_navigation");
    uint32_t cPath     = addConcept("shortest_path");
    uint32_t cObstacle = addConcept("obstacle_avoidance");
    uint32_t cMaze     = addConcept("maze_solving");
    uint32_t cBFS      = addConcept("bfs_algorithm");
    uint32_t cGreedy   = addConcept("greedy_heuristic");
    uint32_t cAstar    = addConcept("astar_search");
    uint32_t cDiag     = addConcept("diagonal_movement");

    // Несколько шаблонных решений
    // Шаблон 1: "идти вправо, потом вниз"
    {
        Solution s;
        int g = problem.gridSize - 1;
        for (int i = 0; i < g; ++i) s.actions.push_back(Action::RIGHT);
        for (int i = 0; i < g; ++i) s.actions.push_back(Action::DOWN);
        uint32_t eid = addHyperedge({cGrid, cPath}, RelationType::SOLUTION_OF,
                                    0.5f, "right_then_down");
        attachSolution(eid, s);
    }

    // Шаблон 2: "идти вниз, потом вправо"
    {
        Solution s;
        int g = problem.gridSize - 1;
        for (int i = 0; i < g; ++i) s.actions.push_back(Action::DOWN);
        for (int i = 0; i < g; ++i) s.actions.push_back(Action::RIGHT);
        uint32_t eid = addHyperedge({cGrid, cPath}, RelationType::SOLUTION_OF,
                                    0.5f, "down_then_right");
        attachSolution(eid, s);
    }

    // Шаблон 3: диагональное чередование
    {
        Solution s;
        int g = problem.gridSize - 1;
        for (int i = 0; i < g; ++i) {
            s.actions.push_back(Action::RIGHT);
            s.actions.push_back(Action::DOWN);
        }
        uint32_t eid = addHyperedge({cGrid, cDiag, cPath}, RelationType::SOLUTION_OF,
                                    0.7f, "diagonal");
        attachSolution(eid, s);
    }

    // Концептуальные связи
    addHyperedge({cMaze, cBFS},    RelationType::ANALOGY,    1.0f, "maze->bfs");
    addHyperedge({cBFS, cAstar},   RelationType::PRECEDENCE, 0.8f, "bfs->astar");
    addHyperedge({cPath, cGreedy}, RelationType::ANALOGY,    0.6f, "path->greedy");
    addHyperedge({cObstacle, cBFS},RelationType::CAUSALITY,  0.9f, "obstacle->bfs");

    RSAD_LOG("[Memory] Loaded %zu concepts, %zu edges\n",
           concepts_.size(), edges_.size());
}

// ---------------------------------------------------------------------------
// Генерация эмбеддинга по умолчанию (детерминированный hash)
// ---------------------------------------------------------------------------
void HypergraphMemory::generateDefaultEmbedding(float* out, const std::string& label) {
    // Простой детерминированный хэш → нормализованный вектор
    uint32_t seed = 0;
    for (char c : label) seed = seed * 31 + static_cast<uint8_t>(c);

    std::mt19937 rng(seed);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    float norm = 0.0f;
    for (int i = 0; i < RSADConfig::EMBED_DIM; ++i) {
        out[i] = dist(rng);
        norm += out[i] * out[i];
    }
    norm = sqrtf(norm) + 1e-9f;
    for (int i = 0; i < RSADConfig::EMBED_DIM; ++i) out[i] /= norm;
}

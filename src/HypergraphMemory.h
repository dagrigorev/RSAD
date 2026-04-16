#pragma once
// =============================================================================
// HypergraphMemory.h — Взвешенная гиперграфовая долговременная память (ДВП)
//
//  Структура:
//    - Концепция (Concept): вершина графа с векторным эмбеддингом
//    - Гиперребро (Hyperedge): связь N концепций + тип отношения + вес
//    - GPU ANN: поиск ближайших концепций через CUDA-ядро (cosine similarity)
//  
//  Для производственного использования ANN можно заменить на cuVS (RAPIDS):
//    #include <cuvs/neighbors/brute_force.hpp>
// =============================================================================

#include "Utils.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <cuda_runtime.h>

// ---------------------------------------------------------------------------
// Типы отношений между концепциями
// ---------------------------------------------------------------------------
enum class RelationType : uint8_t {
    CAUSALITY     = 0,  ///< Причинность
    ANALOGY       = 1,  ///< Аналогия
    PART_WHOLE    = 2,  ///< Часть–целое
    PRECEDENCE    = 3,  ///< Предшествование
    SOLUTION_OF   = 4,  ///< Решение проблемы
    SIMILAR_STRUCT= 5,  ///< Структурное сходство
    METAPHOR      = 6,  ///< Метафора (Mode C)
    CUSTOM        = 7
};

inline const char* relTypeStr(RelationType r) {
    switch(r) {
        case RelationType::CAUSALITY:      return "CAUSALITY";
        case RelationType::ANALOGY:        return "ANALOGY";
        case RelationType::PART_WHOLE:     return "PART_WHOLE";
        case RelationType::PRECEDENCE:     return "PRECEDENCE";
        case RelationType::SOLUTION_OF:    return "SOLUTION_OF";
        case RelationType::SIMILAR_STRUCT: return "SIMILAR_STRUCT";
        case RelationType::METAPHOR:       return "METAPHOR";
        default:                           return "CUSTOM";
    }
}

// ---------------------------------------------------------------------------
// Концепция — узел знания
// ---------------------------------------------------------------------------
struct Concept {
    uint32_t    id       = 0;
    std::string label;                          ///< Читаемое имя
    float       embedding[RSADConfig::EMBED_DIM]; ///< Векторное представление
    float       importance = 1.0f;              ///< Важность (обновляется при использовании)

    Concept() { std::fill(embedding, embedding + RSADConfig::EMBED_DIM, 0.0f); }
};

// ---------------------------------------------------------------------------
// Гиперребро — связь между несколькими концепциями + прикреплённое решение
// ---------------------------------------------------------------------------
struct Hyperedge {
    uint32_t              id = 0;
    std::vector<uint32_t> conceptIds;  ///< IDs связанных концепций
    RelationType          relType  = RelationType::CUSTOM;
    float                 weight   = 1.0f;  ///< Значимость / частота использования
    std::string           label;
    std::string           solutionTag;  ///< Тег прикреплённого шаблона решения
    Solution              solution;     ///< Прикреплённое решение (если есть)
    bool                  hasSolution = false;
};

// ---------------------------------------------------------------------------
// GPU ANN результат
// ---------------------------------------------------------------------------
struct ANNResult {
    uint32_t conceptId;
    float    similarity;
};

// ---------------------------------------------------------------------------
// Гиперграфовая память
// ---------------------------------------------------------------------------
class HypergraphMemory {
public:
    HypergraphMemory();
    ~HypergraphMemory();

    // ---- Управление концепциями ----
    uint32_t addConcept(const std::string& label, const float* embedding = nullptr);
    const Concept* getConcept(uint32_t id) const;
    void updateConceptImportance(uint32_t id, float delta);

    // ---- Управление гиперрёбрами ----
    uint32_t addHyperedge(const std::vector<uint32_t>& conceptIds,
                          RelationType rel,
                          float weight = 1.0f,
                          const std::string& label = "");
    void attachSolution(uint32_t edgeId, const Solution& sol);
    void updateEdgeWeight(uint32_t edgeId, float delta);

    // ---- Поиск ----
    /// Возвращает топ-K концепций по косинусному сходству (GPU)
    std::vector<ANNResult> findSimilarConcepts(const float* queryEmb, int topK) const;

    /// Вычислить структурное сходство гиперребра с проблемой (sigma из теории)
    float computeStructuralSimilarity(uint32_t edgeId,
                                      const std::vector<uint32_t>& problemConceptIds) const;

    /// Возвращает топ-K гиперрёбер по структурному сходству (для Mode A)
    std::vector<std::pair<uint32_t, float>>
    findSimilarEdges(const std::vector<uint32_t>& problemConceptIds, int topK) const;

    // ---- Обновление памяти после решения ----
    void recordSolution(const std::vector<uint32_t>& problemConceptIds,
                        const Solution& sol,
                        float quality);

    // ---- Вспомогательное ----
    size_t numConcepts()  const { return concepts_.size(); }
    size_t numEdges()     const { return edges_.size(); }
    const std::vector<Hyperedge>& getEdges() const { return edges_; }
    const std::vector<Concept>&   getConcepts() const { return concepts_; }

    void print() const;

    // ---- Инициализация демо-базы знаний ----
    void loadDemoKnowledge(const Problem& problem);

private:
    std::vector<Concept>   concepts_;
    std::vector<Hyperedge> edges_;
    uint32_t               nextConceptId_ = 0;
    uint32_t               nextEdgeId_    = 0;

    // GPU-буферы для ANN
    float*   d_embeddings_    = nullptr;  ///< [N x EMBED_DIM] на устройстве
    uint32_t embeddingCount_  = 0;
    bool     gpuDirty_        = true;     ///< Нужно ли перезагружать GPU-буфер

    void uploadEmbeddingsToGPU();
    void freeGPUBuffers();

    // Генерация эмбеддинга по умолчанию (random, для демо)
    void generateDefaultEmbedding(float* out, const std::string& label);
};

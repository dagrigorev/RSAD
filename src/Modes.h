#pragma once
// =============================================================================
// Modes.h — Три режима мышления РСАД
//
//  Режим A: комбинаторная аналогия (поиск в гиперграфе + оператор ⊕)
//  Режим B: обратный ход (BFS от цели к началу, GPU)
//  Режим C: образное мышление (природные метафоры + llama.cpp stub)
// =============================================================================

#include "Utils.h"
#include "HypergraphMemory.h"
#include "WorldModel.h"
#include "BackwardChainer.h"
#include <vector>
#include <string>
#include <memory>

// ---------------------------------------------------------------------------
// Структура природной метафоры (для Mode C)
// ---------------------------------------------------------------------------
struct NatureMetaphor {
    std::string name;        ///< "Муравьиная колония", "Нейрон", "Водный поток"
    std::string description; ///< Текстовое описание
    float       embedding[RSADConfig::EMBED_DIM]; ///< Векторное представление паттерна

    // Ключевые принципы, кодированные как "правила" построения решения
    // Для GridWorld: bias = предпочтение направлений
    float dirBias[4]; ///< [UP, DOWN, LEFT, RIGHT] — смещение вероятности

    NatureMetaphor() {
        std::fill(embedding, embedding + RSADConfig::EMBED_DIM, 0.0f);
        std::fill(dirBias, dirBias + 4, 0.25f);  // равномерное по умолчанию
    }
};

// ---------------------------------------------------------------------------
// Режим A: Комбинаторная аналогия
// ---------------------------------------------------------------------------
class ModeA {
public:
    ModeA(HypergraphMemory& memory, const WorldModel& world);

    /// Основной метод: возвращает пул кандидатов
    std::vector<Solution> generate(const Problem& problem, int kA = 5);

private:
    HypergraphMemory& memory_;
    const WorldModel& world_;

    /// Оператор комбинирования двух решений (⊕)
    Solution combine(const Solution& s1, const Solution& s2,
                     const Problem& problem);

    /// Получить концепции, соответствующие проблеме
    std::vector<uint32_t> extractProblemConcepts(const Problem& problem);
};

// ---------------------------------------------------------------------------
// Режим B: Обратный ход
// ---------------------------------------------------------------------------
class ModeB {
public:
    explicit ModeB(const WorldModel& world);  // инициализирует BackwardChainer

    /// Генерация решений через обратный BFS и вариации
    std::vector<Solution> generate(const Problem& problem, int numPaths = 10);

private:
    const WorldModel& world_;
    BackwardChainer   chainer_;  ///< M^{-1} decomposer

    Solution perturbPath(const std::vector<Action>& base,
                         const Problem& problem, unsigned seed);
};

// ---------------------------------------------------------------------------
// Режим C: Образное мышление (природные метафоры)
// ---------------------------------------------------------------------------
class ModeC {
public:
    ModeC();

    /// Генерация решений на основе природных метафор
    std::vector<Solution> generate(const Problem& problem, int mB = 3);

    // -------------------------------------------------------------------
    // LLM интеграция (llama.cpp)
    // -------------------------------------------------------------------
    // Чтобы включить реальный инференс:
    //  1. Собрать llama.cpp как статическую библиотеку (см. README.md)
    //  2. Раскомментировать RSAD_USE_LLAMACPP в CMakeLists.txt
    //  3. Указать путь к GGUF модели через setModelPath()
    // -------------------------------------------------------------------
    void setModelPath(const std::string& path) { modelPath_ = path; }
    bool loadModel();   ///< Инициализация llama.cpp контекста
    void unloadModel(); ///< Освобождение ресурсов

    /// Запрос к LLM для генерации описания решения
    std::string queryLLM(const std::string& prompt);

private:
    std::vector<NatureMetaphor> metaphors_;
    std::string modelPath_;

    // llama.cpp handle (LlamaHandle*, кастуется через llamacpp_impl.h)
    void* llamaCtx_  = nullptr;  // на самом деле LlamaHandle*
    bool  modelLoaded_ = false;

    void initMetaphors();

    /// Трансформация паттерна метафоры в решение GridWorld
    Solution transformMetaphorToSolution(const NatureMetaphor& m,
                                          const Problem& problem,
                                          unsigned seed);

    /// Косинусное сходство двух векторов (CPU)
    float cosSimilarity(const float* a, const float* b, int dim);

    /// Встроить описание проблемы в вектор (детерминированный)
    void embedProblem(const Problem& problem, float* out);
};

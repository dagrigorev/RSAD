#pragma once
// =============================================================================
// LLMAdvisor.h — LLM как источник информации в активном поиске
//
//  Теория (раздел 6):
//    Когда K_eff < K_min, агент строит запросы к внешней среде.
//    LLM — один из источников, способный генерировать семантически
//    новые решения, а не просто мутации существующих.
//
//  Три режима работы:
//
//  1. REAL_LLM (llama.cpp загружен):
//     - Анализирует текущий пул и проблему
//     - Формирует структурированный prompt с контекстом неудач
//     - Парсит ответ в Solution
//
//  2. STATISTICAL_LLM (fallback, без GPU):
//     - "Синтетический ИИ": анализирует паттерны провалов в пуле
//     - Строит "карту неудач" — регионы сетки, где решения чаще всего ломаются
//     - Генерирует решения, ОБХОДЯЩИЕ эти регионы
//     - Принципиально лучше greedy walk: использует информацию о пуле
//
//  3. PROMPTED_SEARCH (гибрид):
//     - Использует шаблоны "промптов" как алгоритмические инструкции
//     - Каждый шаблон = стратегия поиска (спираль, зигзаг, A*, и т.д.)
//     - Не требует LLM, но эмулирует "рассуждение по аналогии"
// =============================================================================

#include "Utils.h"
#include "WorldModel.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>

// ---------------------------------------------------------------------------
// Карта неудач: какие клетки являются "проблемными"
// ---------------------------------------------------------------------------
struct FailureMap {
    std::vector<float> heatmap;  ///< [H×W]: вероятность неудачи в клетке
    int width, height;
    int worstX, worstY;          ///< Клетка с наибольшей концентрацией отказов
    std::vector<std::pair<int,int>> obstacles; ///< Обнаруженные проблемные регионы

    float at(int x, int y) const {
        if (x<0||x>=width||y<0||y>=height) return 1.0f;
        return heatmap[y*width+x];
    }
};

// ---------------------------------------------------------------------------
// "Промпт" как алгоритмическая стратегия
// ---------------------------------------------------------------------------
struct SearchPrompt {
    std::string name;
    std::string description;
    // Функция генерации пути по этой стратегии
    std::function<Solution(const Problem&, const FailureMap&, unsigned)> generate;
};

// ---------------------------------------------------------------------------
// Главный класс
// ---------------------------------------------------------------------------
class WorldModel;  // forward
class LLMAdvisor {
public:
    explicit LLMAdvisor(void* modeCPtr = nullptr, const WorldModel* world = nullptr);

    /// Основной метод: сгенерировать N новых решений используя LLM/статистику
    std::vector<Solution> advise(
        const std::vector<Solution>& pool,
        const Problem& problem,
        int budget,
        unsigned seed = 0);

    /// Подключить реальный LLM (ModeC::queryLLM)
    void setLLMCallback(std::function<std::string(const std::string&)> cb) {
        llmCallback_ = cb;
        hasRealLLM_  = true;
    }

    /// Описание текущего состояния пула для логирования
    std::string describePool(const std::vector<Solution>& pool,
                              const Problem& problem) const;

private:
    const WorldModel* world_ = nullptr;
    bool hasRealLLM_ = false;
    std::function<std::string(const std::string&)> llmCallback_;

    // ── Режим 1: Реальный LLM ───────────────────────────────────────────────
    std::vector<Solution> queryRealLLM(
        const std::vector<Solution>& pool,
        const Problem& problem,
        int budget, unsigned seed);

    // ── Режим 2: Статистический LLM ─────────────────────────────────────────
    std::vector<Solution> statisticalAdvise(
        const std::vector<Solution>& pool,
        const Problem& problem,
        int budget, unsigned seed);

    /// Построить карту неудач из пула решений
    FailureMap buildFailureMap(
        const std::vector<Solution>& pool,
        const Problem& problem) const;

    /// Генерировать решение, обходящее проблемные регионы
    Solution generateFailureAware(
        const Problem& problem,
        const FailureMap& fm,
        unsigned seed) const;

    // ── Режим 3: Prompted search ─────────────────────────────────────────────
    std::vector<Solution> promptedSearch(
        const Problem& problem,
        const FailureMap& fm,
        int budget, unsigned seed);

    /// Спиральный обход сетки
    Solution spiralWalk(const Problem& p, unsigned seed) const;

    /// Зигзаг: вертикальные полосы
    Solution zigzagWalk(const Problem& p, bool vertical) const;

    /// A*-подобный поиск с настраиваемой эвристикой
    Solution heuristicSearch(const Problem& p, float obstacleWeight, unsigned seed) const;

    /// Разбивка на квадранты + решение каждого
    Solution quadrantDecompose(const Problem& p, unsigned seed) const;

    // Инициализация промптов
    std::vector<SearchPrompt> prompts_;
    void initPrompts();

    // Internal BFS helper
    std::vector<Action> world_bfs(int fx,int fy,int tx,int ty,const Problem& p) const;
    std::vector<Action> world_bfs_impl(int fx,int fy,int tx,int ty,const Problem& p) const;

    // Парсинг текстового ответа LLM в Solution
    Solution parseLLMResponse(const std::string& resp,
                               const Problem& problem) const;
};

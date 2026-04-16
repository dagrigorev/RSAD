#pragma once
// =============================================================================
// WorldInventor.h — РСАД-изобретатель новых симуляций
//
//  Использует три режима мышления:
//  A (аналогия):  "Это похоже на X, но с правилом Y"
//  B (обратный):  "Что за мир порождает такое поведение?"
//  C (метафора):  "Нейроны → Fire-CA; Кровеносная система → Fluid+Tree"
//
//  Результат: SimulationSpec готовый к кодогенерации
// =============================================================================
#include "SimulationSpec.h"
#include <string>
#include <vector>
#include <random>
#include <unordered_map>

// Концепт изобретения (узел гиперграфа идей)
struct SimConcept {
    std::string  name;
    std::string  description;
    SimType      baseType;
    float        embedding[32];  // упрощённый эмбеддинг

    // Параметрические вариации
    std::unordered_map<std::string, SimParam> defaultParams;
    std::unordered_map<std::string, SimParam> variantParams;
};

// Запрос на изобретение
struct InventionRequest {
    std::string  userQuery;        // Текстовый запрос
    std::string  theme;            // "space", "biology", "matrix", "chaos" ...
    bool         forceNovel = false; // Придумать что-то принципиально новое
    int          seed = 0;
};

class WorldInventor {
public:
    WorldInventor();

    // ── Основные методы ───────────────────────────────────────────────────

    /// Придумать симуляцию по запросу пользователя
    SimulationSpec invent(const InventionRequest& req);

    /// Придумать гибридный мир (комбинация двух+ типов)
    SimulationSpec inventHybrid(SimType a, SimType b,
                                 const std::string& theme = "");

    /// Создать предустановленную симуляцию по имени
    SimulationSpec preset(const std::string& name);

    /// Список доступных пресетов
    std::vector<std::string> listPresets() const;

    /// Парсинг запроса → тип симуляции + параметры
    SimulationSpec parseUserQuery(const std::string& query);

private:
    std::mt19937 rng_;
    std::vector<SimConcept> concepts_;
    std::vector<std::string> presetNames_;

    void initConcepts();
    void initPresets();

    // Режим A: найти похожий концепт и адаптировать
    SimulationSpec modeA_analogy(const InventionRequest& req);
    // Режим B: обратный ход от желаемого поведения
    SimulationSpec modeB_backward(const InventionRequest& req);
    // Режим C: природная метафора
    SimulationSpec modeC_metaphor(const InventionRequest& req);

    // Вспомогательные
    SimType        detectTypeFromQuery(const std::string& q);
    std::string    makeId(const std::string& base) const;
    float          noveltyScore(const SimulationSpec& s) const;
    void           applyTheme(SimulationSpec& spec, const std::string& theme);

    // Параметрические мутации
    SimulationSpec mutateSpec(const SimulationSpec& base, float amount);
    void           randomizeCARule(CARule& rule, int seed);
};

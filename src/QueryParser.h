#pragma once
// =============================================================================
// QueryParser.h — Понимание запроса пользователя
//
//  Задача: естественный язык (рус./англ.) → struct Problem
//
//  Пример входа:
//    "найди путь в сетке 10 на 10, старт (0,0), цель (9,9), 20% препятствий"
//    "hard maze 15x15 from top-left to bottom-right"
//    "grid 8 obstacles 30%"
//    "простая задача"
//
//  Архитектура:
//    1. Токенизация + нормализация
//    2. Keyword slot-filling (regex + словари)
//    3. Intent classification (keyword voting)
//    4. Slot → Problem mapping
//    5. Confidence score
// =============================================================================

#include "Utils.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

// ---------------------------------------------------------------------------
// Намерение пользователя
// ---------------------------------------------------------------------------
enum class QueryIntent {
    SOLVE_PATHFINDING,    ///< Найти путь
    SET_DIFFICULTY,       ///< Задать сложность
    SHOW_STATS,           ///< Показать статистику
    TRAIN,                ///< Запустить обучение
    HELP,                 ///< Помощь
    UNKNOWN
};

// ---------------------------------------------------------------------------
// Извлечённые слоты из запроса
// ---------------------------------------------------------------------------
struct QuerySlots {
    std::optional<int>   gridSize;       ///< Размер сетки
    std::optional<int>   startX, startY; ///< Координаты старта
    std::optional<int>   goalX,  goalY;  ///< Координаты цели
    std::optional<float> obstacleDensity;///< Плотность препятствий (0..1)
    std::optional<int>   seed;           ///< Seed для генератора
    std::optional<int>   trainEpisodes;  ///< Кол-во эпизодов обучения
    std::string          difficultyTag;  ///< "easy"/"medium"/"hard"/"extreme"
    QueryIntent          intent = QueryIntent::UNKNOWN;
    float                confidence = 0.0f;
    std::string          rawQuery;
    std::string          explanation;    ///< Что было понято
};

// ---------------------------------------------------------------------------
// Результат парсинга
// ---------------------------------------------------------------------------
struct ParseResult {
    QuerySlots slots;
    Problem    problem;
    bool       valid = false;
    std::string errorMsg;
};

// ---------------------------------------------------------------------------
// Главный класс парсера
// ---------------------------------------------------------------------------
class QueryParser {
public:
    QueryParser();

    /// Основной метод: текст → ParseResult
    ParseResult parse(const std::string& query) const;

    /// Сформировать Problem из частично заполненных слотов + defaults
    Problem buildProblem(const QuerySlots& slots) const;

    /// Объяснить что было распознано (для вывода пользователю)
    std::string explain(const QuerySlots& slots) const;

private:
    // ---- Предобработка ----
    std::string normalize(const std::string& s) const;
    std::vector<std::string> tokenize(const std::string& s) const;

    // ---- Извлечение слотов ----
    void extractNumbers   (const std::string& norm, QuerySlots& out) const;
    void extractCoords    (const std::string& norm, QuerySlots& out) const;
    void extractGridSize  (const std::string& norm,
                           const std::vector<std::string>& tokens,
                           QuerySlots& out) const;
    void extractDensity   (const std::string& norm, QuerySlots& out) const;
    void extractDifficulty(const std::string& norm, QuerySlots& out) const;
    void extractIntent    (const std::string& norm,
                           const std::vector<std::string>& tokens,
                           QuerySlots& out) const;
    void applyDifficultyDefaults(QuerySlots& out) const;

    // ---- Словари (рус + англ) ----
    struct Keyword { std::string word; float weight; };
    std::unordered_map<std::string, std::vector<Keyword>> intentKeywords_;

    void initKeywords();

    // Вспомогательные
    bool containsAny(const std::string& s,
                     const std::vector<std::string>& words) const;
    std::optional<int>   extractIntAfter(const std::string& s,
                                          const std::string& marker) const;
    std::optional<float> extractFloatAfter(const std::string& s,
                                            const std::string& marker) const;
};

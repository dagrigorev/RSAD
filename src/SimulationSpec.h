#pragma once
// =============================================================================
// SimulationSpec.h — Структурированное описание симуляции
//
//  Каждая симуляция описывается набором параметров и правил.
//  CodeGenerator берёт SimulationSpec и генерирует готовый C++ файл,
//  который можно скомпилировать и запустить в терминале.
//
//  Поддерживаемые типы:
//    GAME_OF_LIFE    — клеточный автомат Конвея (и варианты)
//    LANGTON_ANT     — муравей Лэнгтона
//    PARTICLE_GRAVITY— частицы с гравитацией
//    FOREST_FIRE     — лесной пожар
//    ECOSYSTEM       — хищник/жертва/растения
//    WAVE            — волновое уравнение
//    CRYSTAL         — рост кристалла (DLA)
//    NEURAL_FIRE     — нейронное возбуждение
//    MATRIX_RAIN     — цифровой дождь (в честь Матрицы)
//    FLUID_CA        — жидкость на клеточном автомате
//    BOIDS           — стайное поведение
//    HYBRID          — изобретённая World Inventor комбинация
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

// ---------------------------------------------------------------------------
// Тип симуляции
// ---------------------------------------------------------------------------
enum class SimType {
    GAME_OF_LIFE,
    LANGTON_ANT,
    PARTICLE_GRAVITY,
    FOREST_FIRE,
    ECOSYSTEM,
    WAVE,
    CRYSTAL,
    NEURAL_FIRE,
    MATRIX_RAIN,
    FLUID_CA,
    BOIDS,
    HYBRID
};

inline const char* simTypeName(SimType t) {
    switch(t) {
        case SimType::GAME_OF_LIFE:     return "Game of Life";
        case SimType::LANGTON_ANT:      return "Langton's Ant";
        case SimType::PARTICLE_GRAVITY: return "Particle Gravity";
        case SimType::FOREST_FIRE:      return "Forest Fire";
        case SimType::ECOSYSTEM:        return "Ecosystem";
        case SimType::WAVE:             return "Wave";
        case SimType::CRYSTAL:          return "Crystal Growth";
        case SimType::NEURAL_FIRE:      return "Neural Firing";
        case SimType::MATRIX_RAIN:      return "Matrix Rain";
        case SimType::FLUID_CA:         return "Fluid CA";
        case SimType::BOIDS:            return "Boids";
        case SimType::HYBRID:           return "Hybrid (Invented)";
        default:                        return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Параметр симуляции — унифицированное значение
// ---------------------------------------------------------------------------
using SimParam = std::variant<int, float, bool, std::string>;

inline int    paramInt  (const SimParam& p) { return std::get<int>(p);         }
inline float  paramFloat(const SimParam& p) { return std::get<float>(p);       }
inline bool   paramBool (const SimParam& p) { return std::get<bool>(p);        }
inline const std::string& paramStr(const SimParam& p){ return std::get<std::string>(p); }

// ---------------------------------------------------------------------------
// Правило клеточного автомата: "B3/S23" → birth=[3], survive=[2,3]
// ---------------------------------------------------------------------------
struct CARule {
    std::vector<int> birth;    ///< Кол-во соседей для рождения
    std::vector<int> survive;  ///< Кол-во соседей для выживания
    int  numStates = 2;        ///< Кол-во состояний (2 = бинарный)
    std::string name = "Life"; ///< Название правила
};

// ---------------------------------------------------------------------------
// Описание вида (для Ecosystem)
// ---------------------------------------------------------------------------
struct Species {
    std::string name;
    char        symbol;        ///< ASCII символ
    int         colorCode;     ///< ANSI цвет (31..37)
    float       birthRate;
    float       deathRate;
    std::string prey;          ///< Имя вида-жертвы (если хищник)
    float       eatProbability;
};

// ---------------------------------------------------------------------------
// Главная структура SimulationSpec
// ---------------------------------------------------------------------------
struct SimulationSpec {
    // ── Мета-информация ───────────────────────────────────────────────────
    std::string  id;           ///< Уникальный идентификатор
    std::string  title;        ///< Человекочитаемое название
    std::string  description;  ///< Описание мира (для пользователя)
    std::string  author;       ///< "user" | "WorldInventor"
    SimType      type = SimType::GAME_OF_LIFE;

    // ── Параметры мира ────────────────────────────────────────────────────
    int   width     = 80;      ///< Ширина мира
    int   height    = 40;      ///< Высота мира
    int   frames    = 200;     ///< Кол-во кадров симуляции
    int   delayMs   = 80;      ///< Задержка между кадрами (мс)
    bool  wrapEdges = true;    ///< Тороидальная топология

    // ── Параметры типа ────────────────────────────────────────────────────
    std::unordered_map<std::string, SimParam> params;

    // ── Правила CA ────────────────────────────────────────────────────────
    CARule caRule;             ///< Для GAME_OF_LIFE и вариантов

    // ── Экосистема ────────────────────────────────────────────────────────
    std::vector<Species> species;

    // ── Hybrid: описание комбинации ───────────────────────────────────────
    std::vector<SimType> hybridComponents; ///< Из чего состоит гибрид
    std::string          hybridLogic;      ///< Описание логики гибрида

    // ── Метаданные изобретения ────────────────────────────────────────────
    float creativityScore = 0.0f; ///< 0..1: насколько необычна идея
    std::vector<std::string> inspirations; ///< Концепции-источники

    // ── Вспомогательные ──────────────────────────────────────────────────
    void setParam(const std::string& k, SimParam v) { params[k] = v; }

    bool hasParam(const std::string& k) const {
        return params.find(k) != params.end();
    }

    SimParam getParam(const std::string& k, SimParam def) const {
        auto it = params.find(k);
        return it != params.end() ? it->second : def;
    }

    void print() const {
        printf("\n┌──────────────────────────────────────────────┐\n");
        printf("│  Симуляция: %-33s│\n", title.c_str());
        printf("│  Тип:       %-33s│\n", simTypeName(type));
        printf("│  Размер:    %dx%-30d │\n", width, height);
        printf("│  Кадров:    %-33d│\n", frames);
        printf("│  Автор:     %-33s│\n", author.c_str());
        if (creativityScore > 0)
            printf("│  Новизна:   %.2f%-30s│\n", creativityScore, "");
        printf("│  %s\n", description.c_str());
        if (!inspirations.empty()) {
            printf("│  Источники: ");
            for (auto& s : inspirations) printf("%s  ", s.c_str());
            printf("\n");
        }
        printf("└──────────────────────────────────────────────┘\n");
    }
};

#pragma once
// =============================================================================
// CodeGenerator.h — Генератор C++ кода симуляций
//
//  Берёт SimulationSpec и производит самодостаточный .cpp файл,
//  который компилируется одной командой g++ -O2 -o sim sim.cpp
//  и выводит симуляцию в терминал через ANSI escape codes.
// =============================================================================

#include "SimulationSpec.h"
#include <string>

// Результат генерации
struct GeneratedCode {
    std::string  sourceCode;      ///< Полный .cpp файл
    std::string  filename;        ///< Имя файла (без расширения)
    std::string  compileCmd;      ///< Команда компиляции
    std::string  runCmd;          ///< Команда запуска
    bool         success = false;
    std::string  errorMsg;
    int          linesOfCode = 0;
};

class CodeGenerator {
public:
    CodeGenerator();

    // ── Основной метод ────────────────────────────────────────────────────
    /// Генерировать код для симуляции
    GeneratedCode generate(const SimulationSpec& spec,
                           const std::string& outputDir = "/tmp/rsad_sims");

    // ── Отдельные генераторы по типу ──────────────────────────────────────
    std::string genGameOfLife   (const SimulationSpec& s);
    std::string genLangtonAnt   (const SimulationSpec& s);
    std::string genParticleGrav (const SimulationSpec& s);
    std::string genForestFire   (const SimulationSpec& s);
    std::string genEcosystem    (const SimulationSpec& s);
    std::string genWave         (const SimulationSpec& s);
    std::string genCrystal      (const SimulationSpec& s);
    std::string genNeuralFire   (const SimulationSpec& s);
    std::string genMatrixRain   (const SimulationSpec& s);
    std::string genFluidCA      (const SimulationSpec& s);
    std::string genBoids        (const SimulationSpec& s);
    std::string genHybrid       (const SimulationSpec& s);

private:
    // ── Общие блоки кода (вставляются во все симуляции) ─────────────────
    std::string commonHeader(const SimulationSpec& s) const;
    std::string commonFooter()                         const;
    std::string ansiHelpers()                          const;
    std::string terminalInit()                         const;
    std::string sleepHelper()                          const;

    // ── Вспомогательные ─────────────────────────────────────────────────
    std::string caRuleCode    (const CARule& rule)     const;
    std::string speciesCode   (const std::vector<Species>& sp) const;
    int         countLines    (const std::string& code) const;

    std::string outputDir_;
};

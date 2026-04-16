#pragma once
// =============================================================================
// SimShell.h — Интерактивная оболочка для SimulationForge
// =============================================================================
#include "WorldInventor.h"
#include "CodeGenerator.h"
#include "SimulationRunner.h"
#include <string>
#include <memory>
#include <vector>

class SimShell {
public:
    SimShell();

    void run();   ///< Интерактивный REPL
    std::string processCommand(const std::string& cmd); ///< Для тестов

private:
    WorldInventor   inventor_;
    CodeGenerator   codegen_;
    SimulationRunner runner_;

    // История изобретений текущей сессии
    struct HistoryEntry {
        SimulationSpec spec;
        GeneratedCode  code;
        bool           compiled = false;
    };
    std::vector<HistoryEntry> history_;
    SimulationSpec lastSpec_;

    void printWelcome()  const;
    void printHelp()     const;
    void printPresets()  const;
    void printHistory()  const;

    std::string cmdInvent (const std::string& args);
    std::string cmdPreset (const std::string& name);
    std::string cmdRun    (const std::string& args);
    std::string cmdCode   (const std::string& args);
    std::string cmdList   ();
    std::string cmdHybrid (const std::string& args);
};

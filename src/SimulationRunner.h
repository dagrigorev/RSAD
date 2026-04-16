#pragma once
// =============================================================================
// SimulationRunner.h — Компиляция и запуск сгенерированных симуляций
// =============================================================================
#include "CodeGenerator.h"
#include <string>

struct RunResult {
    bool   compiled  = false;
    bool   ran       = false;
    int    exitCode  = 0;
    std::string compileOutput;
    std::string binaryPath;
};

class SimulationRunner {
public:
    explicit SimulationRunner(const std::string& workDir = "/tmp/rsad_sims");

    /// Скомпилировать сгенерированный код
    RunResult compile(const GeneratedCode& code);

    /// Запустить скомпилированный бинарник (в текущем терминале)
    RunResult run(const RunResult& compiled, int maxFrames = 0);

    /// Полный цикл: compile + run
    RunResult compileAndRun(const GeneratedCode& code);

    /// Показать исходный код
    void showCode(const GeneratedCode& code, int maxLines = 80) const;

    std::string getWorkDir() const { return workDir_; }

private:
    std::string workDir_;
};

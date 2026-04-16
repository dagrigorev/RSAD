// =============================================================================
// SimulationRunner.cpp
// =============================================================================
#include "SimulationRunner.h"
#include "platform_compat.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/stat.h>

SimulationRunner::SimulationRunner(const std::string& workDir)
    : workDir_(workDir) {
    mkdir(workDir_.c_str(), 0755);
}

RunResult SimulationRunner::compile(const GeneratedCode& code) {
    RunResult result;
    if (!code.success) {
        result.compileOutput = "Code generation failed: " + code.errorMsg;
        return result;
    }

    printf("[Runner] Компилирую: %s\n", code.compileCmd.c_str());
    printf("[Runner] Файл: %s.cpp (%d строк)\n",
           code.filename.c_str(), code.linesOfCode);

    // Компиляция
    std::string cmd = code.compileCmd + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.compileOutput = "popen failed";
        return result;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe))
        result.compileOutput += buf;
    int ret = pclose(pipe);

    result.compiled = (ret == 0);
    result.binaryPath = code.filename;

    if (result.compiled)
        printf("[Runner] ✓ Компиляция успешна → %s\n", result.binaryPath.c_str());
    else
        printf("[Runner] ✗ Ошибка компиляции:\n%s\n", result.compileOutput.c_str());

    return result;
}

RunResult SimulationRunner::run(const RunResult& compiled, int /*maxFrames*/) {
    RunResult result = compiled;
    if (!compiled.compiled) return result;

    printf("[Runner] Запуск симуляции... (Ctrl+C для остановки)\n");
    printf("[Runner] %s\n\n", compiled.binaryPath.c_str());

    // Небольшая пауза перед запуском
    usleep(300000);

    // Запускаем в текущем терминале (пользователь видит анимацию)
    result.exitCode = system(compiled.binaryPath.c_str());
    result.ran = true;

    // Восстанавливаем курсор на случай если симуляция завершилась нештатно
    printf("\033[?25h\033[0m\n");

    return result;
}

RunResult SimulationRunner::compileAndRun(const GeneratedCode& code) {
    auto cr = compile(code);
    if (!cr.compiled) return cr;
    return run(cr);
}

void SimulationRunner::showCode(const GeneratedCode& code, int maxLines) const {
    printf("\n┌──────────────────────────────────────────────────────────\n");
    printf("│ Сгенерированный код: %d строк\n", code.linesOfCode);
    printf("│ Файл: %s.cpp\n", code.filename.c_str());
    printf("│ Компиляция: %s\n", code.compileCmd.c_str());
    printf("└──────────────────────────────────────────────────────────\n");

    std::istringstream ss(code.sourceCode);
    std::string line;
    int lineNum = 0;
    while (std::getline(ss, line) && lineNum < maxLines) {
        printf("%4d│ %s\n", ++lineNum, line.c_str());
    }
    if (code.linesOfCode > maxLines)
        printf("  ... (%d строк скрыто)\n", code.linesOfCode - maxLines);
    printf("\n");
}

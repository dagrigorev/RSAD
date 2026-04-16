#pragma once
// =============================================================================
// InteractiveShell.h — Интерактивная оболочка (REPL)
// =============================================================================
#include "Utils.h"
#include "RSADAgent.h"
#include "QueryParser.h"
#include "Trainer.h"
#include <string>
#include <memory>
#include <vector>

class InteractiveShell {
public:
    InteractiveShell();

    /// Запуск цикла ввода-вывода
    void run();

    /// Обработка одного запроса (для тестирования)
    std::string processQuery(const std::string& query);

private:
    std::unique_ptr<RSADAgent>   agent_;
    std::unique_ptr<QueryParser> parser_;
    TrainingStats                trainingStats_;
    bool                         trained_      = false;
    int                          totalSolved_  = 0;
    int                          totalSuccess_ = 0;
    std::vector<std::string>     history_;

    void printWelcome() const;
    void printHelp()    const;
    void printStats()   const;

    std::string handleSolve(const ParseResult& pr);
    std::string handleTrain(const ParseResult& pr);
    std::string handleStats() const;
};

// =============================================================================
// InteractiveShell.cpp — Реализация интерактивной оболочки РСАД
// =============================================================================
#include "InteractiveShell.h"
#include "LearningCurve.h"
#include "WorldModel.h"
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <sstream>

InteractiveShell::InteractiveShell()
    : agent_(std::make_unique<RSADAgent>()),
      parser_(std::make_unique<QueryParser>())
{}

// ---------------------------------------------------------------------------
void InteractiveShell::printWelcome() const {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║   РСАД — Резонансный Синтез с Активным Доопределением   ║\n");
    printf("║               Интерактивный режим v1.0                  ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\nЯ понимаю запросы на русском и английском языках.\n");
    printf("Введите запрос или 'помощь' / 'help' для справки.\n\n");
}

void InteractiveShell::printHelp() const {
    printf("\n┌─────────────────────────────────────────────────────┐\n");
    printf("│                   Примеры запросов                   │\n");
    printf("├─────────────────────────────────────────────────────┤\n");
    printf("│ ПОИСК ПУТИ:                                         │\n");
    printf("│  найди путь в сетке 10x10                           │\n");
    printf("│  реши лабиринт 8x8, 25%% препятствий                 │\n");
    printf("│  hard maze 12x12 from (0,0) to (11,11)             │\n");
    printf("│  сложная задача 15x15                               │\n");
    printf("│  find path grid 10 obstacles 30%%                    │\n");
    printf("│  простая сетка 6x6                                  │\n");
    printf("│                                                     │\n");
    printf("│ ОБУЧЕНИЕ:                                           │\n");
    printf("│  обучи агента 50 эпизодов                           │\n");
    printf("│  train 100 episodes                                 │\n");
    printf("│  тренируй 30 итераций                               │\n");
    printf("│                                                     │\n");
    printf("│ ИНФОРМАЦИЯ:                                         │\n");
    printf("│  статистика / stats                                 │\n");
    printf("│  память / memory                                    │\n");
    printf("│  выход / quit / exit                                │\n");
    printf("└─────────────────────────────────────────────────────┘\n\n");
}

void InteractiveShell::printStats() const {
    printf("\n┌─────────────────────────────────────────┐\n");
    printf("│          Текущая статистика              │\n");
    printf("├─────────────────────────────────────────┤\n");
    printf("│  Всего решено задач: %-5d               │\n", totalSolved_);
    printf("│  Успешно:            %-5d (%.0f%%)         │\n",
           totalSuccess_,
           totalSolved_>0 ? (float)totalSuccess_/totalSolved_*100 : 0);
    printf("│  Обучен:             %s               │\n",
           trained_ ? "да ✓" : "нет  ");
    if (trained_) {
        printf("│  После обучения SR:  %.0f%%              │\n",
               trainingStats_.successRate()*100);
        printf("│  Лучшее качество:    %.3f              │\n",
               trainingStats_.bestQualityEver);
    }
    printf("│  Знаний в памяти:                       │\n");
    printf("│    Концепций: %-5zu                      │\n",
           agent_->getMemory().numConcepts());
    printf("│    Паттернов: %-5zu                      │\n",
           agent_->getMemory().numEdges());
    printf("└─────────────────────────────────────────┘\n\n");
}

// ---------------------------------------------------------------------------
std::string InteractiveShell::handleSolve(const ParseResult& pr) {
    std::ostringstream out;
    const Problem& problem = pr.problem;

    out << "\n" << pr.slots.explanation << "\n";
    out << "Сетка " << problem.gridSize << "×" << problem.gridSize
        << ", старт(" << problem.startX << "," << problem.startY << ")"
        << " → цель(" << problem.goalX << "," << problem.goalY << ")\n\n";

    printf("%s", out.str().c_str());

    // Визуализируем задачу
    printf("Карта:\n");
    problem.print();
    printf("\n");

    // Запускаем агента
    Solution sol = agent_->solve(problem);
    const AgentStats& stats = agent_->getStats();

    totalSolved_++;
    if (stats.found) totalSuccess_++;

    std::ostringstream res;
    res << "\n";
    if (stats.found) {
        res << "✓ Решение найдено!\n";
        res << "  Оценка:     " << sol.totalScore << "\n";
        res << "    e1(цель)=" << sol.e1
            << "  e2(перспект)=" << sol.e2
            << "  e3(элегант)=" << sol.e3
            << "  e4(безвред)=" << sol.e4 << "\n";
        res << "  Длина пути: " << sol.actions.size() << " шагов\n";
        res << "  K_eff:      " << stats.finalKEff << " различимых решений\n";
        res << "  Время:      " << stats.timeSeconds*1000 << " мс\n";
        res << "  Путь: " << sol.toString() << "\n";
    } else {
        res << "✗ Приемлемое решение не найдено (возможно, путь заблокирован)\n";
        if (!sol.actions.empty()) {
            res << "  Лучшее из найденного: score=" << sol.totalScore
                << ", len=" << sol.actions.size() << "\n";
        }
    }
    printf("%s", res.str().c_str());

    // Рисуем путь
    if (!sol.actions.empty()) {
        printf("\nВизуализация пути:\n");
        WorldModel tmpW(problem);
        tmpW.printPath(sol);
    }
    printf("\n");

    return res.str();
}

std::string InteractiveShell::handleTrain(const ParseResult& pr) {
    int episodes = pr.slots.trainEpisodes.value_or(50);
    printf("\n▶ Запуск обучения на %d эпизодах...\n", episodes);
    printf("  (агент будет решать задачи нарастающей сложности)\n\n");

    Trainer trainer(*agent_, episodes, true);
    trainer.setPruneInterval(10);
    trainingStats_ = trainer.run();
    trained_ = true;
    trainer.getCurve().printTable(std::max(1,episodes/8));

    std::ostringstream res;
    res << "\nОбучение завершено!\n";
    res << "  Успешность: " << (int)(trainingStats_.successRate()*100) << "%\n";
    res << "  Лучшее качество: " << trainingStats_.bestQualityEver << "\n";
    res << "  Паттернов в памяти: " << agent_->getMemory().numEdges() << "\n";

    printf("%s\n", res.str().c_str());
    return res.str();
}

// ---------------------------------------------------------------------------
std::string InteractiveShell::processQuery(const std::string& query) {
    history_.push_back(query);

    // Специальные команды
    std::string q = query;
    std::transform(q.begin(),q.end(),q.begin(),[](unsigned char c){
        return (c<128)?tolower(c):c;
    });

    if (q=="выход"||q=="quit"||q=="exit"||q=="q") return "EXIT";
    if (q=="помощь"||q=="help"||q=="?")    { printHelp(); return ""; }
    if (q=="статистика"||q=="stats"||q=="память"||q=="memory") {
        printStats(); return "";
    }
    if (q.empty()) return "";

    // Парсинг
    ParseResult pr = parser_->parse(query);

    if (!pr.valid) {
        printf("Не смог разобрать запрос: %s\n", pr.errorMsg.c_str());
        printf("Введите 'помощь' для списка команд.\n");
        return "";
    }

    switch(pr.slots.intent) {
        case QueryIntent::SOLVE_PATHFINDING:
            return handleSolve(pr);
        case QueryIntent::TRAIN:
            return handleTrain(pr);
        case QueryIntent::SHOW_STATS:
            printStats(); return "";
        case QueryIntent::HELP:
            printHelp(); return "";
        default:
            // Если не распознали intent но есть grid-параметры — пробуем решить
            if (pr.slots.gridSize || pr.slots.goalX) {
                pr.slots.intent = QueryIntent::SOLVE_PATHFINDING;
                return handleSolve(pr);
            }
            printf("Не понял запрос. Попробуйте 'помощь'.\n");
            return "";
    }
}

// ---------------------------------------------------------------------------
void InteractiveShell::run() {
    printWelcome();

    std::string line;
    while (true) {
        printf("РСАД> ");
        fflush(stdout);

        if (!std::getline(std::cin, line)) break;  // EOF

        // Убираем trailing whitespace
        while (!line.empty() && (line.back()=='\n'||line.back()=='\r'||line.back()==' '))
            line.pop_back();

        std::string result = processQuery(line);
        if (result == "EXIT") {
            printf("\nДо свидания! (решено задач: %d, успешно: %d)\n",
                   totalSolved_, totalSuccess_);
            break;
        }
    }
}

// =============================================================================
// SimShell.cpp — Реализация оболочки SimulationForge
// =============================================================================
#include "SimShell.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>

SimShell::SimShell() : runner_("/tmp/rsad_sims") {}

void SimShell::printWelcome() const {
    printf("\033[2J\033[H");
    printf("\033[32m\033[1m");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           РСАД  WorldForge — Генератор Симуляций            ║\n");
    printf("║        \"Я могу показать тебе Матрицу...\" — RSAD v3         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    printf("Я придумываю новые симуляции и генерирую для них C++ код.\n");
    printf("Введите \033[33mпомощь\033[0m или \033[33mhelp\033[0m для справки.\n\n");
}

void SimShell::printHelp() const {
    printf("\n\033[36m┌─────────────────────────────────────────────────────────┐\033[0m\n");
    printf("\033[36m│                Команды WorldForge                       │\033[0m\n");
    printf("\033[36m├─────────────────────────────────────────────────────────┤\033[0m\n");
    printf("│ \033[33mпридумай <описание>\033[0m  — изобрести новый мир              │\n");
    printf("│ \033[33minvent <description>\033[0m — invent a new world               │\n");
    printf("│ \033[33mпресет <имя>\033[0m         — загрузить готовую симуляцию       │\n");
    printf("│ \033[33mpreset <name>\033[0m                                           │\n");
    printf("│ \033[33mгибрид <тип1> <тип2>\033[0m — создать гибридный мир           │\n");
    printf("│ \033[33mhybrid <type1> <type2>\033[0m                                  │\n");
    printf("│ \033[33mзапусти\033[0m / \033[33mrun\033[0m       — скомпилировать и запустить        │\n");
    printf("│ \033[33mкод\033[0m / \033[33mcode\033[0m          — показать сгенерированный код      │\n");
    printf("│ \033[33mсписок\033[0m / \033[33mlist\033[0m       — список пресетов                   │\n");
    printf("│ \033[33mистория\033[0m / \033[33mhistory\033[0m   — история изобретений               │\n");
    printf("│ \033[33mвыход\033[0m / \033[33mquit\033[0m                                           │\n");
    printf("\033[36m├─────────────────────────────────────────────────────────┤\033[0m\n");
    printf("│ Примеры запросов:                                        │\n");
    printf("│   \033[32mпридумай мир с нейронами\033[0m                              │\n");
    printf("│   \033[32minvent ocean wave simulation\033[0m                          │\n");
    printf("│   \033[32mпридумай что-то с хаосом и птицами\033[0m                   │\n");
    printf("│   \033[32mпресет matrix\033[0m                                         │\n");
    printf("│   \033[32mгибрид life wave\033[0m                                      │\n");
    printf("│   \033[32mпридумай новый мир\033[0m  (максимальная новизна)            │\n");
    printf("\033[36m└─────────────────────────────────────────────────────────┘\033[0m\n\n");
}

void SimShell::printPresets() const {
    auto presets = inventor_.listPresets();
    printf("\n\033[33mДоступные пресеты (%zu):\033[0m\n", presets.size());
    int col = 0;
    for (auto& p : presets) {
        printf("  %-20s", p.c_str());
        if (++col % 3 == 0) printf("\n");
    }
    printf("\n\n");
}

void SimShell::printHistory() const {
    if (history_.empty()) { printf("История пуста.\n"); return; }
    printf("\n\033[33mИстория изобретений:\033[0m\n");
    for (int i = 0; i < (int)history_.size(); i++) {
        auto& h = history_[i];
        printf("  [%d] %-30s  тип=%-15s  новизна=%.2f  %s\n",
               i+1, h.spec.title.c_str(), simTypeName(h.spec.type),
               h.spec.creativityScore,
               h.compiled ? "\033[32m[скомпилирован]\033[0m" : "[не скомпилирован]");
    }
    printf("\n");
}

// ---------------------------------------------------------------------------
// Команда: придумай
// ---------------------------------------------------------------------------
std::string SimShell::cmdInvent(const std::string& args) {
    if (args.empty()) {
        return "Укажите описание мира. Пример: придумай мир с нейронами";
    }

    InventionRequest req;
    req.userQuery  = args;
    req.forceNovel = (args.find("новый") != std::string::npos ||
                      args.find("novel") != std::string::npos);

    SimulationSpec spec = inventor_.invent(req);
    spec.print();
    lastSpec_ = spec;

    // Генерируем код
    GeneratedCode code = codegen_.generate(spec);
    if (!code.success) return "Ошибка генерации кода: " + code.errorMsg;

    printf("\n\033[32m✓ Код сгенерирован: %d строк\033[0m\n", code.linesOfCode);
    printf("  Файл: \033[36m%s.cpp\033[0m\n", code.filename.c_str());
    printf("\nВведите \033[33mзапусти\033[0m чтобы скомпилировать и запустить.\n");
    printf("Введите \033[33mкод\033[0m чтобы посмотреть исходник.\n\n");

    HistoryEntry entry{spec, code, false};
    history_.push_back(entry);

    return "";
}

// ---------------------------------------------------------------------------
// Команда: пресет
// ---------------------------------------------------------------------------
std::string SimShell::cmdPreset(const std::string& name) {
    if (name.empty()) { printPresets(); return ""; }

    SimulationSpec spec = inventor_.preset(name);
    if (spec.id.empty()) return "Пресет не найден: " + name;

    spec.print();
    lastSpec_ = spec;

    GeneratedCode code = codegen_.generate(spec);
    if (!code.success) return "Ошибка генерации: " + code.errorMsg;

    printf("\n\033[32m✓ Пресет '%s' загружен, код сгенерирован (%d строк)\033[0m\n",
           name.c_str(), code.linesOfCode);
    printf("Введите \033[33mзапусти\033[0m для запуска.\n\n");

    history_.push_back({spec, code, false});
    return "";
}

// ---------------------------------------------------------------------------
// Команда: гибрид
// ---------------------------------------------------------------------------
std::string SimShell::cmdHybrid(const std::string& args) {
    // Парсим два типа
    static const std::pair<const char*, SimType> typeMap[] = {
        {"life",SimType::GAME_OF_LIFE},{"конвей",SimType::GAME_OF_LIFE},
        {"ant",SimType::LANGTON_ANT},{"муравей",SimType::LANGTON_ANT},
        {"particle",SimType::PARTICLE_GRAVITY},{"частиц",SimType::PARTICLE_GRAVITY},
        {"fire",SimType::FOREST_FIRE},{"пожар",SimType::FOREST_FIRE},
        {"ecosystem",SimType::ECOSYSTEM},{"экосистем",SimType::ECOSYSTEM},
        {"wave",SimType::WAVE},{"волн",SimType::WAVE},
        {"crystal",SimType::CRYSTAL},{"кристалл",SimType::CRYSTAL},
        {"neural",SimType::NEURAL_FIRE},{"нейрон",SimType::NEURAL_FIRE},
        {"matrix",SimType::MATRIX_RAIN},{"матрикс",SimType::MATRIX_RAIN},
        {"fluid",SimType::FLUID_CA},{"жидкост",SimType::FLUID_CA},
        {"boids",SimType::BOIDS},{"стая",SimType::BOIDS},
    };

    SimType a = SimType::GAME_OF_LIFE, b = SimType::WAVE;
    int found = 0;
    for (auto& [kw, t] : typeMap) {
        if (args.find(kw) != std::string::npos) {
            if (found==0) a=t; else b=t;
            found++;
            if (found==2) break;
        }
    }
    if (found < 2) {
        // Случайный выбор двух разных типов
        std::mt19937 rng(time(nullptr));
        a = static_cast<SimType>(rng()%11);
        b = static_cast<SimType>((rng()%10 + (int)a+1) % 11);
    }

    SimulationSpec spec = inventor_.inventHybrid(a, b);
    spec.print();
    lastSpec_ = spec;

    GeneratedCode code = codegen_.generate(spec);
    if (!code.success) return "Ошибка генерации: " + code.errorMsg;

    printf("\n\033[32m✓ Гибридный мир создан (%d строк кода)\033[0m\n", code.linesOfCode);
    printf("Введите \033[33mзапусти\033[0m для запуска.\n\n");

    history_.push_back({spec, code, false});
    return "";
}

// ---------------------------------------------------------------------------
// Команда: запусти
// ---------------------------------------------------------------------------
std::string SimShell::cmdRun(const std::string& /*args*/) {
    if (history_.empty()) return "Сначала придумайте или выберите симуляцию.";

    auto& last = history_.back();

    // Компилируем если ещё не скомпилировано
    if (!last.compiled) {
        auto cr = runner_.compile(last.code);
        if (!cr.compiled)
            return "Ошибка компиляции:\n" + cr.compileOutput;
        last.compiled = true;
        last.code.filename = cr.binaryPath;
    }

    // Запускаем
    printf("\n\033[33m▶ Запуск симуляции \"%s\"...\033[0m\n\n",
           last.spec.title.c_str());
    printf("(Дождитесь завершения или нажмите Ctrl+C)\n\n");

    RunResult cr;
    cr.compiled   = true;
    cr.binaryPath = last.code.filename;
    runner_.run(cr);

    return "";
}

// ---------------------------------------------------------------------------
// Команда: код
// ---------------------------------------------------------------------------
std::string SimShell::cmdCode(const std::string& args) {
    if (history_.empty()) return "Нет сгенерированного кода.";
    int maxLines = 60;
    if (!args.empty()) {
        try { maxLines = std::stoi(args); } catch(...) {}
    }
    runner_.showCode(history_.back().code, maxLines);
    return "";
}

// ---------------------------------------------------------------------------
// processCommand
// ---------------------------------------------------------------------------
std::string SimShell::processCommand(const std::string& cmd) {
    if (cmd.empty()) return "";

    std::string lower = cmd;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return c<128?tolower(c):c; });

    // Выход
    if (lower=="выход"||lower=="quit"||lower=="exit"||lower=="q")
        return "EXIT";

    // Помощь
    if (lower=="помощь"||lower=="help"||lower=="?") { printHelp(); return ""; }

    // Список
    if (lower=="список"||lower=="list"||lower=="ls") { printPresets(); return ""; }

    // История
    if (lower=="история"||lower=="history") { printHistory(); return ""; }

    // Код
    if (lower=="код"||lower=="code"||lower.substr(0,4)=="code") {
        std::string args = (lower.size()>5) ? cmd.substr(5) : "";
        return cmdCode(args);
    }

    // Запуск
    if (lower=="запусти"||lower=="run"||lower=="запустить"||lower=="старт")
        return cmdRun("");

    // Пресет
    if (lower.substr(0,6)=="пресет" || lower.substr(0,6)=="preset") {
        std::string name = (cmd.size()>7) ? cmd.substr(7) : "";
        // Убираем пробелы
        while (!name.empty() && name.front()==' ') name.erase(name.begin());
        return cmdPreset(name);
    }

    // Гибрид
    if (lower.substr(0,6)=="гибрид" || lower.substr(0,6)=="hybrid") {
        std::string args = (cmd.size()>7) ? cmd.substr(7) : "";
        return cmdHybrid(args);
    }

    // Придумай / invent
    if (lower.substr(0,8)=="придумай" || lower.substr(0,6)=="invent"
        || lower.substr(0,8)=="создай м" || lower.substr(0,9)=="сгенерир") {
        // Извлекаем аргументы
        size_t spacePos = cmd.find(' ');
        std::string args = (spacePos != std::string::npos) ? cmd.substr(spacePos+1) : cmd;
        return cmdInvent(args);
    }

    // Если не распознали — пробуем как запрос на изобретение
    if (cmd.size() > 3) {
        printf("\033[33m[SimShell] Интерпретирую как запрос на изобретение...\033[0m\n");
        return cmdInvent(cmd);
    }

    return "Неизвестная команда. Введите 'помощь'.";
}

// ---------------------------------------------------------------------------
// run() — главный REPL
// ---------------------------------------------------------------------------
void SimShell::run() {
    printWelcome();
    std::string line;
    while (true) {
        printf("\033[35mWorldForge\033[0m> ");
        fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        while (!line.empty() && (line.back()=='\r'||line.back()=='\n'||line.back()==' '))
            line.pop_back();
        std::string res = processCommand(line);
        if (res == "EXIT") {
            printf("\n\033[32mДо свидания! Создано миров: %zu\033[0m\n\n",
                   history_.size());
            break;
        }
        if (!res.empty()) printf("%s\n", res.c_str());
    }
}

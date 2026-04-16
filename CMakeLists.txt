cmake_minimum_required(VERSION 3.21)

# ============================================================
#  РСАД — Резонансный Синтез с Активным Доопределением
#  CUDA/C++ проект для Visual Studio 2026 / Windows 11 x64
# ============================================================
project(RSAD_Demo LANGUAGES CXX CUDA)

# ---- Стандарты ----
set(CMAKE_CXX_STANDARD  17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Версия CUDA (требуется 12.x) ----
find_package(CUDAToolkit 12.0 REQUIRED)
message(STATUS "CUDA Toolkit: ${CUDAToolkit_VERSION}")
message(STATUS "CUDA Include: ${CUDAToolkit_INCLUDE_DIRS}")

# ---- Поиск пакета CCCL (включает в себя Thrust, CUB и другие) ----
# Минимальная версия может отличаться, для CUDA 13.2 подойдёт 2.7.0 или выше
find_package(CCCL 3.2.0 REQUIRED)

# ----  Опции сборки ----
option(RSAD_USE_LLAMACPP "Включить интеграцию с llama.cpp" OFF)
option(RSAD_VERBOSE      "Подробный вывод компилятора"     OFF)

# ---- Исходные файлы ----
set(RSAD_SOURCES
    src/main.cpp
    src/HypergraphMemory.cu
    src/WorldModel.cu
    src/Modes.cu
    src/Evaluator.cu
    src/ActiveSearcher.cu
    src/RSADAgent.cu
    src/CodeGenerator.cpp
    src/WorldInventor.cpp
    src/SimulationRunner.cpp
    src/SimShell.cpp
    src/Utils_global.cpp
    src/QueryParser.cpp
    src/Trainer.cpp
    src/InteractiveShell.cpp
    src/LearningCurve.cpp
    src/MemoryPruner.cpp
    src/BackwardChainer.cpp
    src/ProblemBank.cpp
    src/LLMAdvisor.cpp
)

set(RSAD_HEADERS
    src/Utils.h
    src/HypergraphMemory.h
    src/WorldModel.h
    src/Modes.h
    src/Evaluator.h
    src/ActiveSearcher.h
    src/RSADAgent.h
)

# ---- Основной исполняемый файл ----
add_executable(rsad_demo ${RSAD_SOURCES} ${RSAD_HEADERS})

# ---- CUDA архитектуры (настройте под вашу GPU) ----
# Поддерживаемые: 70 (V100), 75 (RTX 2xxx), 80 (A100), 86 (RTX 3xxx), 89 (RTX 4xxx), 90 (H100)
set_target_properties(rsad_demo PROPERTIES
    CUDA_ARCHITECTURES          "75;80;86;89"
    CUDA_SEPARABLE_COMPILATION  ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

# ---- Флаги компилятора ----
target_compile_options(rsad_demo PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --extended-lambda
        --expt-relaxed-constexpr
        -Xcompiler "/W3"
        --ptxas-options=-v
        -lineinfo
    >
    $<$<COMPILE_LANGUAGE:CXX>:
        /W3
        /MP          # Параллельная сборка
        /EHsc
        /utf-8       # UTF-8 исходники (кириллица в комментариях)
    >
)

# Оптимизация для Release, отладочная для Debug
target_compile_options(rsad_demo PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Release>>: -O3>
    $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Debug>>:   -G -g>
)

# ---- Include директории ----
target_include_directories(rsad_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CUDAToolkit_INCLUDE_DIRS}
)

# ---- Линковка с CUDA библиотеками ----
target_link_libraries(rsad_demo PRIVATE
    CUDA::cudart
    CUDA::cublas
    CUDA::cusparse
    CCCL::Thrust
)

# ---- cuVS (опционально, из RAPIDS) ----
# Раскомментируйте если установлен cuVS:
# find_package(cuvs QUIET)
# if(cuvs_FOUND)
#     target_link_libraries(rsad_demo PRIVATE cuvs::cuvs)
#     target_compile_definitions(rsad_demo PRIVATE RSAD_USE_CUVS)
#     message(STATUS "cuVS found — ANN acceleration enabled")
# else()
#     message(STATUS "cuVS NOT found — using built-in CUDA ANN")
# endif()

# ---- llama.cpp интеграция (опционально) ----
if(RSAD_USE_LLAMACPP)
    message(STATUS "Включена поддержка llama.cpp")

    # Укажите путь к собранному llama.cpp:
    # cmake -DRSAD_USE_LLAMACPP=ON -DLLAMA_DIR=C:/libs/llama.cpp ..
    if(NOT DEFINED LLAMA_DIR)
        message(FATAL_ERROR
            "RSAD_USE_LLAMACPP=ON требует указания LLAMA_DIR. "
            "Пример: cmake -DLLAMA_DIR=C:/libs/llama.cpp ..")
    endif()

    set(LLAMA_INCLUDE "${LLAMA_DIR}/include")
    # Современный llama.cpp (b3000+): ggml заголовки в отдельной папке
    set(GGML_INCLUDE  "${LLAMA_DIR}/ggml/include")
    # Попытка найти библиотеку (llama.lib или llama_static.lib)
    find_library(LLAMA_LIB
        NAMES llama llama_static
        PATHS "${LLAMA_DIR}/build" "${LLAMA_DIR}/lib"
        NO_DEFAULT_PATH)

    if(NOT LLAMA_LIB)
        message(FATAL_ERROR
            "Не найдена llama.lib в ${LLAMA_DIR}. "
            "Соберите llama.cpp: cd llama.cpp && cmake -B build -DGGML_CUDA=ON && cmake --build build --config Release")
    endif()

    target_include_directories(rsad_demo PRIVATE ${LLAMA_INCLUDE} ${GGML_INCLUDE})

    # Ищем ggml.lib
    find_library(GGML_LIB
        NAMES ggml ggml_static
        PATHS "${LLAMA_DIR}/build/ggml/src/Release"
              "${LLAMA_DIR}/build/ggml/src"
              "${LLAMA_DIR}/build/ggml/Release"
              "${LLAMA_DIR}/build/Release"
              "${LLAMA_DIR}/build/src/Release"
        NO_DEFAULT_PATH)

    # ── ВАЖНО: llama.lib и ggml.lib — CPU-только библиотеки.
    # Их нельзя передавать nvlink (CUDA device linker).
    # Используем IMPORTED targets с CUDA_SEPARABLE_COMPILATION=OFF,
    # чтобы они участвовали только в финальной host-линковке.

    add_library(rsad_llama STATIC IMPORTED)
    set_target_properties(rsad_llama PROPERTIES
        IMPORTED_LOCATION             "${LLAMA_LIB}"
        CUDA_SEPARABLE_COMPILATION    OFF
        IMPORTED_NO_SONAME            TRUE
    )

    if(GGML_LIB)
        message(STATUS "ggml.lib: ${GGML_LIB}")
        add_library(rsad_ggml STATIC IMPORTED)
        set_target_properties(rsad_ggml PROPERTIES
            IMPORTED_LOCATION          "${GGML_LIB}"
            CUDA_SEPARABLE_COMPILATION OFF
            IMPORTED_NO_SONAME         TRUE
        )
        target_link_libraries(rsad_demo PRIVATE rsad_llama rsad_ggml)
    else()
        message(STATUS "ggml.lib not found, linking llama only")
        target_link_libraries(rsad_demo PRIVATE rsad_llama)
    endif()
    target_compile_definitions(rsad_demo PRIVATE RSAD_USE_LLAMACPP)
    message(STATUS "llama.cpp: ${LLAMA_LIB}")
endif()

# ---- nlohmann/json (header-only) ----
# Если установлен через vcpkg или вручную:
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(rsad_demo PRIVATE nlohmann_json::nlohmann_json)
    message(STATUS "nlohmann/json found")
else()
    # Попытка найти в include/
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/nlohmann/json.hpp")
        message(STATUS "nlohmann/json found in include/")
    else()
        message(WARNING
            "nlohmann/json не найден. "
            "Скачайте json.hpp в include/nlohmann/: "
            "https://github.com/nlohmann/json/releases")
    endif()
endif()

# ---- Вывод конфигурации ----
message(STATUS "")
message(STATUS "=== RSAD Build Configuration ===")
message(STATUS "  Project:     ${PROJECT_NAME}")
message(STATUS "  Build type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  CUDA arches: 75;80;86;89")
message(STATUS "  llama.cpp:   ${RSAD_USE_LLAMACPP}")
message(STATUS "  Output:      $<TARGET_FILE:rsad_demo>")
message(STATUS "================================")
message(STATUS "")

# ---- Установка рабочей директории (для Visual Studio) ----
set_property(TARGET rsad_demo PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- Копирование ресурсов ----
add_custom_command(TARGET rsad_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:rsad_demo>/data"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:rsad_demo>/models"
    COMMENT "Creating resource directories..."
)

# ── Windows-специфичные настройки ────────────────────────────────────────────
if(WIN32)
    target_compile_definitions(rsad_demo PRIVATE
        _CRT_SECURE_NO_WARNINGS      # отключить предупреждения о _s функциях
        NOMINMAX                     # не переопределять min/max
        WIN32_LEAN_AND_MEAN          # исключить лишние Windows заголовки
    )
    # На Windows usleep не существует — platform_compat.h заменяет на Sleep()
    # mkdir не существует — заменяем на _mkdir через platform_compat.h
    target_link_libraries(rsad_demo PRIVATE ws2_32)  # Winsock (для network future)
endif()


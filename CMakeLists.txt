cmake_minimum_required(VERSION 3.16)
project(RPG3DRussianLedger VERSION 0.2 LANGUAGES CXX)

# Простой стек: GCC C++17, SQLite, OpenSSL Crypto и (только для графики) raylib.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RPG_BUILD_CLIENT "Собрать raylib-клиент" ON)
option(RPG_BUILD_CORE_CLI "Собрать прежнюю консольную игру v3" ON)
option(RPG_BUILD_STUB_SMOKE "Собрать клиент с тестовой заглушкой raylib" OFF)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(rpg_client_api INTERFACE)
target_include_directories(rpg_client_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Хранилище header-only. Единственное реальное включение — в Application.
add_library(rpg_game_deps INTERFACE)
target_link_libraries(rpg_game_deps INTERFACE rpg_client_api SQLite::SQLite3 OpenSSL::Crypto)

if(RPG_BUILD_CLIENT)
    find_package(raylib CONFIG QUIET)
    if(TARGET raylib)
        set(RPG_RAYLIB_TARGET raylib)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)
        set(RPG_RAYLIB_TARGET PkgConfig::RAYLIB)
    endif()
    add_executable(rpg_3d src/main.cpp)
    target_link_libraries(rpg_3d PRIVATE rpg_game_deps ${RPG_RAYLIB_TARGET})
endif()

# Независимый legacy CLI: пока не переносим его старые сохранения в SQLite.
add_library(rpg_core_v3 INTERFACE)
target_include_directories(rpg_core_v3 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core_v3/include")
if(RPG_BUILD_CORE_CLI)
    add_executable(rpg_core_cli core_v3/cli_main.cpp)
    target_link_libraries(rpg_core_cli PRIVATE rpg_core_v3)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_modules tests/test_modules.cpp)
    target_link_libraries(test_modules PRIVATE rpg_client_api)
    add_test(NAME client_modules COMMAND test_modules)

    add_executable(test_economy_ledger tests/test_ledger.cpp)
    target_link_libraries(test_economy_ledger PRIVATE rpg_game_deps)
    add_test(NAME economy_ledger COMMAND test_economy_ledger)

    add_executable(test_core_crafting core_v3/tests/test_crafting.cpp)
    target_link_libraries(test_core_crafting PRIVATE rpg_core_v3)
    add_test(NAME legacy_crafting COMMAND test_core_crafting)

    add_executable(test_core_alchemy core_v3/tests/test_alchemy.cpp)
    target_link_libraries(test_core_alchemy PRIVATE rpg_core_v3)
    add_test(NAME legacy_alchemy COMMAND test_core_alchemy)

    # Нельзя потерять проверки assert при включённой оптимизации Release.
    foreach(t test_modules test_economy_ledger test_core_crafting test_core_alchemy)
        if(MSVC)
            target_compile_options(${t} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${t} PRIVATE -UNDEBUG)
        endif()
    endforeach()

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME module_independence COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_dependencies.py")
    endif()
endif()

if(RPG_BUILD_STUB_SMOKE)
    add_executable(test_client_stub src/main.cpp)
    target_include_directories(test_client_stub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/raylib_stub")
    target_link_libraries(test_client_stub PRIVATE rpg_game_deps)
    if(BUILD_TESTING)
        add_test(NAME headless_client_frame COMMAND test_client_stub)
        add_test(NAME headless_cli_verify COMMAND test_client_stub --verify)
        add_test(NAME headless_cli_inventory COMMAND test_client_stub --inventory)
    endif()
endif()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Генерация Doxygen-документации")
endif()

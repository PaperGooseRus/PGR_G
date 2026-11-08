cmake_minimum_required(VERSION 3.16)
project(RPG3DCubeVillage VERSION 0.1 LANGUAGES CXX)

# Минимальная конфигурация: никаких пакетных менеджеров C++ и генераторов кода.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RPG_BUILD_CLIENT "Build raylib 3D client" ON)
option(RPG_BUILD_CORE_CLI "Build preserved core v3 CLI" ON)
option(RPG_BUILD_STUB_SMOKE "Compile and run client using headless raylib stub" OFF)

# Все независимые модули header-only. Компонуются только в src/main.cpp.
add_library(rpg_client_api INTERFACE)
target_include_directories(rpg_client_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(RPG_BUILD_CLIENT)
    find_package(raylib CONFIG REQUIRED)
    add_executable(rpg_3d src/main.cpp)
    target_link_libraries(rpg_3d PRIVATE rpg_client_api raylib)
endif()

# Сохранённое RPG Core v3 — отдельный legacy-компонент: пока нет 3D-адаптера.
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

    add_executable(test_core_crafting core_v3/tests/test_crafting.cpp)
    target_link_libraries(test_core_crafting PRIVATE rpg_core_v3)
    add_test(NAME previous_core_crafting COMMAND test_core_crafting)

    add_executable(test_core_alchemy core_v3/tests/test_alchemy.cpp)
    target_link_libraries(test_core_alchemy PRIVATE rpg_core_v3)
    add_test(NAME previous_core_alchemy COMMAND test_core_alchemy)

    # assert из тестов нельзя отключать даже в Release.
    foreach(t test_modules test_core_crafting test_core_alchemy)
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

# Вспомогательная компиляция интерфейса raylib без графики. Используется для
# проверки C++ в средах, где нет настоящей raylib; не является игрой.
if(RPG_BUILD_STUB_SMOKE)
    add_executable(test_client_stub src/main.cpp)
    target_include_directories(test_client_stub PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/raylib_stub")
    target_link_libraries(test_client_stub PRIVATE rpg_client_api)
    if(BUILD_TESTING)
        add_test(NAME client_stub_smoke COMMAND test_client_stub)
    endif()
endif()

# Doxygen необязателен для сборки программы.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generate internal C++ API documentation")
endif()

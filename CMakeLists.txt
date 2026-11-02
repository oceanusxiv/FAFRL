cmake_minimum_required(VERSION 3.28)

project(faf_rl LANGUAGES C CXX)

# --- Build-wide settings ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
# compile_commands.json for editors (VS Code's C/C++ extension, clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    if(MSVC)
        # MSVC syntax for AddressSanitizer
        add_compile_options(/fsanitize=address /Zi)
        add_compile_definitions(_DISABLE_VECTOR_ANNOTATION) # Prevents false positives in std::vector
        # Note: MSVC handles linking ASan libraries automatically via /fsanitize=address
    else()
        # GCC / Clang syntax
        add_compile_options(-fsanitize=address,leak -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,leak)
    endif()
endif()

# --- Engine -----------------------------------------------------------------------------
add_subdirectory(dependencies/faf-re)

find_package(Python 3.14 COMPONENTS Interpreter Development.Module REQUIRED)
add_subdirectory(dependencies/nanobind)

add_subdirectory(cpp/faf_repy)

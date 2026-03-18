cmake_minimum_required(VERSION 3.20)
project(socialist LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(socialist STATIC 
    src/socialist.cpp 
    src/socialist.hpp
)

include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v5.0.0 # O una versione più recente
)
FetchContent_MakeAvailable(ftxui)


target_include_directories(socialist PUBLIC src)

find_package(nlohmann_json QUIET)
target_link_libraries(socialist PUBLIC nlohmann_json::nlohmann_json ftxui::screen ftxui::dom ftxui::component)
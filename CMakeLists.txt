cmake_minimum_required(VERSION 3.20)
project(Synchrona LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Add both projects
add_subdirectory(engine)
add_subdirectory(game)
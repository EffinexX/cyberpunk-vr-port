# Offline tests of the hand and gun solver against the game's own PhysX3Common_x64.dll. Not part of the mod's build:
#   cmake -S tests/physics -B build/physics_test -A x64
#   cmake --build build/physics_test --config Release
#   build/physics_test/Release/physics_test.exe
cmake_minimum_required(VERSION 3.20)
project(physics_test CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(physics_test main.cpp)
target_include_directories(physics_test PRIVATE ../../include)

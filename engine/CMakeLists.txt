cmake_minimum_required(VERSION 3.20)
project(trading_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(TE_BUILD_TESTS "Build unit tests" ON)
option(TE_BUILD_BENCH "Build benchmark" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(te_core
  src/sim_exchange.cpp
  src/portfolio.cpp
  src/data_source.cpp
  src/engine.cpp
  src/metrics.cpp
  src/results_io.cpp
  src/strategies/market_maker.cpp
  src/strategies/momentum.cpp
  src/strategies/registry.cpp
)
target_include_directories(te_core PUBLIC include)
set_target_properties(te_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(te_backtest apps/backtest_main.cpp)
target_link_libraries(te_backtest PRIVATE te_core)

if(TE_BUILD_BENCH)
  add_executable(te_bench bench/bench_engine.cpp)
  target_link_libraries(te_bench PRIVATE te_core)
endif()

if(TE_BUILD_TESTS)
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
      GIT_SHALLOW TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  add_executable(te_tests
    tests/test_event_queue.cpp
    tests/test_sim_exchange.cpp
    tests/test_portfolio.cpp
    tests/test_engine.cpp
    tests/test_data_source.cpp
    tests/test_strategies.cpp
  )
  target_link_libraries(te_tests PRIVATE te_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(te_tests)
endif()

cmake_minimum_required(VERSION 3.16)
project(cubesolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets VS Code IntelliSense see the real flags

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CUBESOLVER_BUILD_TESTS "Build unit tests" ON)
option(CUBESOLVER_BUILD_SERVER "Build the HTTP server and web demo" ON)
option(CUBESOLVER_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(CUBESOLVER_TSAN "Build with ThreadSanitizer (GCC/Clang)" OFF)

find_package(Threads REQUIRED)

# Warnings for our own code only.
function(cubesolver_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
  endif()
endfunction()

if(CUBESOLVER_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC reports a false positive inside its own <regex> header in sanitizer builds.
    add_compile_options(-Wno-maybe-uninitialized)
  endif()
endif()
if(CUBESOLVER_TSAN AND NOT MSVC)
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
endif()

# ---------------------------------------------------------------- core library
add_library(cubesolver_core
  src/core/cubie.cpp
  src/core/facelet.cpp
  src/core/coord.cpp
  src/core/tables.cpp
  src/core/solver.cpp
)
target_include_directories(cubesolver_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cubesolver_core PUBLIC Threads::Threads)
cubesolver_warnings(cubesolver_core)

# ---------------------------------------------------------------- CLI
add_executable(cubesolver src/cli/main.cpp)
target_link_libraries(cubesolver PRIVATE cubesolver_core)
cubesolver_warnings(cubesolver)

# ---------------------------------------------------------------- web server
if(CUBESOLVER_BUILD_SERVER)
  add_library(cubesolver_api STATIC src/server/api.cpp src/server/counters.cpp)
  target_include_directories(cubesolver_api SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_link_libraries(cubesolver_api PUBLIC cubesolver_core)

  # HTTPS lets the server save its visitor counts to Upstash. Optional: without
  # OpenSSL 3 everything else still works.
  option(CUBESOLVER_HTTPS "Use OpenSSL 3 if found, for HTTPS calls from the server" ON)
  if(CUBESOLVER_HTTPS)
    if(APPLE AND NOT OPENSSL_ROOT_DIR)
      foreach(dir /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
        if(EXISTS ${dir})
          set(OPENSSL_ROOT_DIR ${dir})
          break()
        endif()
      endforeach()
    endif()
    find_package(OpenSSL 3)
    if(OpenSSL_FOUND)
      message(STATUS "HTTPS support: on (OpenSSL ${OPENSSL_VERSION})")
      # PUBLIC: every file that includes httplib.h must see the same setting.
      target_compile_definitions(cubesolver_api PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
      target_link_libraries(cubesolver_api PUBLIC OpenSSL::SSL OpenSSL::Crypto)
      if(APPLE)
        # httplib reads the trusted certificates from the macOS keychain.
        target_link_libraries(cubesolver_api PUBLIC "-framework CoreFoundation" "-framework Security" "-framework CFNetwork")
      endif()
    else()
      message(STATUS "HTTPS support: off (OpenSSL 3 not found)")
    endif()
  endif()
  if(WIN32)
    target_compile_definitions(cubesolver_api PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(cubesolver_api PUBLIC ws2_32)
  endif()
  cubesolver_warnings(cubesolver_api)

  add_executable(cubesolver_server src/server/main.cpp)
  target_link_libraries(cubesolver_server PRIVATE cubesolver_api)
  target_compile_definitions(cubesolver_server PRIVATE CUBESOLVER_WEB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web")
  cubesolver_warnings(cubesolver_server)

  # Copy the web demo next to the server on every build, so the build folder
  # also works on its own (for example after moving it to another machine).
  add_custom_target(web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/web
    COMMENT "Copying web demo files")
endif()

# ---------------------------------------------------------------- tests
if(CUBESOLVER_BUILD_TESTS)
  enable_testing()
  add_executable(cubesolver_tests
    tests/test_main.cpp
    tests/test_cubie.cpp
    tests/test_facelet.cpp
    tests/test_tables.cpp
    tests/test_solver.cpp
  )
  target_include_directories(cubesolver_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_link_libraries(cubesolver_tests PRIVATE cubesolver_core)
  if(CUBESOLVER_BUILD_SERVER)
    target_sources(cubesolver_tests PRIVATE tests/test_api.cpp)
    target_link_libraries(cubesolver_tests PRIVATE cubesolver_api)
  endif()
  cubesolver_warnings(cubesolver_tests)
  add_test(NAME unit_tests COMMAND cubesolver_tests)

  # Checks that the web demo's JavaScript cube model turns faces exactly like
  # the C++ model. Runs only if Node.js is installed.
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE)
    add_test(NAME web_model_matches_cpp
      COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_web_model.mjs $<TARGET_FILE:cubesolver>)
  endif()
endif()

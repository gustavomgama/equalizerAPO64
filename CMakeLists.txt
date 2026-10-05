cmake_minimum_required(VERSION 3.20)
project(EqualizerAPO-Linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# muparserx: system install if present, else build the fork from source.
find_path(MUPARSERX_INCLUDE_DIR mpParser.h)
find_library(MUPARSERX_LIBRARY NAMES muparserx)
if(MUPARSERX_LIBRARY AND MUPARSERX_INCLUDE_DIR)
  set(MUPARSERX_LINK ${MUPARSERX_LIBRARY})
  set(MUPARSERX_INCLUDES ${MUPARSERX_INCLUDE_DIR})
else()
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(USE_WIDE_STRING ON CACHE BOOL "" FORCE)
  FetchContent_Declare(muparserx
    GIT_REPOSITORY https://github.com/thefirekahuna/muparserx
    GIT_TAG 0a48779615103ca5baa301be9eefecdf23156ec6)
  FetchContent_MakeAvailable(muparserx)
  set(MUPARSERX_LINK muparserx)
  set(MUPARSERX_INCLUDES ${muparserx_SOURCE_DIR}/parser)
endif()

add_library(eqapo_core STATIC
  helpers/MemoryHelper.cpp
  helpers/LogHelper.cpp
  helpers/ConfigPathHelper.cpp
)
target_include_directories(eqapo_core PUBLIC
  ${CMAKE_SOURCE_DIR} ${MUPARSERX_INCLUDES} ${FFTW3_INCLUDE_DIRS} ${SNDFILE_INCLUDE_DIRS}
)
target_compile_definitions(eqapo_core PUBLIC MUP_USE_WIDE_STRING)
target_link_libraries(eqapo_core PUBLIC
  ${FFTW3_LIBRARIES} ${SNDFILE_LIBRARIES} ${MUPARSERX_LINK} dl pthread
)

enable_testing()
add_executable(test_platform tests/test_platform.cpp)
target_link_libraries(test_platform PRIVATE eqapo_core)
add_test(NAME platform COMMAND test_platform)
add_test(NAME windows_untouched COMMAND bash ${CMAKE_SOURCE_DIR}/tests/check_windows_untouched.sh)

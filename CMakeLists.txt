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
  FilterEngine.cpp
  FilterConfiguration.cpp
  IFilter.cpp
  helpers/MemoryHelper.cpp
  helpers/LogHelper.cpp
  helpers/StringHelper.cpp
  helpers/ChannelHelper.cpp
  helpers/GainIterator.cpp
  helpers/ConfigPathHelper.cpp
  parser/LogicalOperators.cpp
  parser/RegexFunctions.cpp
  parser/StringOperators.cpp
  parser/RegistryFunctions.cpp
  filters/BiQuad.cpp
  filters/BiQuadFilter.cpp
  filters/BiQuadFilterFactory.cpp
  filters/ChannelFilter.cpp
  filters/ChannelFilterFactory.cpp
  filters/ConvolutionFilter.cpp
  filters/ConvolutionFilterFactory.cpp
  filters/CopyFilter.cpp
  filters/CopyFilterFactory.cpp
  filters/DelayFilter.cpp
  filters/DelayFilterFactory.cpp
  filters/DeviceFilterFactory.cpp
  filters/ExpressionFilterFactory.cpp
  filters/GraphicEQFilter.cpp
  filters/GraphicEQFilterFactory.cpp
  filters/IfFilterFactory.cpp
  filters/IIRFilter.cpp
  filters/IIRFilterFactory.cpp
  filters/IncludeFilterFactory.cpp
  filters/PreampFilter.cpp
  filters/PreampFilterFactory.cpp
  filters/StageFilterFactory.cpp
  filters/VSTPluginFilter.cpp
  filters/VSTPluginFilterFactory.cpp
  filters/loudnessCorrection/LoudnessCorrectionFilter.cpp
  filters/loudnessCorrection/LoudnessCorrectionFilterFactory.cpp
  filters/loudnessCorrection/VolumeController.cpp
  helpers/VSTPluginInstance.cpp
  helpers/VSTPluginLibrary.cpp
  helpers/AbstractLibrary.cpp
  libHybridConv-0.1.1/libHybridConv_eapo.cpp
)
target_include_directories(eqapo_core PUBLIC
  ${CMAKE_SOURCE_DIR} ${MUPARSERX_INCLUDES} ${FFTW3_INCLUDE_DIRS} ${SNDFILE_INCLUDE_DIRS}
)
target_compile_definitions(eqapo_core PUBLIC MUP_USE_WIDE_STRING)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
  target_compile_options(eqapo_core PUBLIC -mavx2 -mfma -mf16c)
endif()
target_link_libraries(eqapo_core PUBLIC
  ${FFTW3_LIBRARIES} fftw3_threads ${SNDFILE_LIBRARIES} ${MUPARSERX_LINK} dl pthread
)

enable_testing()
add_executable(test_platform tests/test_platform.cpp)
target_link_libraries(test_platform PRIVATE eqapo_core)
add_test(NAME platform COMMAND test_platform)
add_test(NAME windows_untouched COMMAND bash ${CMAKE_SOURCE_DIR}/tests/check_windows_untouched.sh)

add_executable(test_strings tests/test_strings.cpp)
target_link_libraries(test_strings PRIVATE eqapo_core)
add_test(NAME strings COMMAND test_strings)

add_executable(test_engine_preamp tests/test_engine_preamp.cpp)
target_link_libraries(test_engine_preamp PRIVATE eqapo_core)
add_test(NAME engine_preamp COMMAND test_engine_preamp)

add_executable(test_filters tests/test_filters.cpp)
target_link_libraries(test_filters PRIVATE eqapo_core)
add_test(NAME filters COMMAND test_filters)

add_executable(eqapo-null linux/nullhost/main.cpp)
target_link_libraries(eqapo-null PRIVATE eqapo_core)
add_test(NAME null_e2e COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_null_e2e.sh $<TARGET_FILE:eqapo-null>)

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
add_executable(eqapo-host linux/host/main.cpp)
target_link_libraries(eqapo-host PRIVATE eqapo_core ${PIPEWIRE_LIBRARIES})
target_include_directories(eqapo-host PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
add_test(NAME host_e2e COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_host_e2e.sh $<TARGET_FILE:eqapo-host>)

add_library(eqapo-vst-stub SHARED linux/vst-test/stub_plugin.cpp)
target_include_directories(eqapo-vst-stub PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(eqapo-vst-stub PROPERTIES PREFIX "")
add_test(NAME vst_e2e COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_vst_e2e.sh $<TARGET_FILE:eqapo-null> $<TARGET_FILE:eqapo-vst-stub>)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
add_subdirectory(Editor)

# Installation (Linux). Installs the host, WAV tool, and Editor, plus the
# systemd user unit and desktop entry.
install(TARGETS eqapo-host eqapo-null eqapo-editor RUNTIME DESTINATION bin)
install(FILES linux/host/eqapo-host.service DESTINATION share/equalizerapo)
install(FILES linux/eqapo-editor.desktop DESTINATION share/applications)

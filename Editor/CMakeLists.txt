# Linux build of the Equalizer APO configuration editor.
#
# This file is added from the root CMakeLists.txt via add_subdirectory(Editor)
# and expects the eqapo_core static library target to already exist.
#
# The Windows build continues to use Editor/Editor.pro and is not affected.

cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/guis
)

set(EDITOR_SOURCES
  main.cpp
  IFilterGUIFactory.cpp
  IFilterGUI.cpp
  FilterTable.cpp
  FilterTableMimeData.cpp
  FilterTableRow.cpp
  FilterTemplate.cpp
  MainWindow.cpp
  AnalysisPlotScene.cpp
  AnalysisPlotView.cpp
  AnalysisThread.cpp
  CustomStyle.cpp
  helpers/GUIChannelHelper.cpp
  helpers/GUIHelper.cpp
  helpers/DisableWheelFilter.cpp
  helpers/QtSndfileHandle.cpp
  widgets/ChannelGraphItem.cpp
  widgets/ChannelGraphScene.cpp
  widgets/CompactToolBar.cpp
  widgets/EscapableLineEdit.cpp
  widgets/ExponentialSpinBox.cpp
  widgets/FrequencyPlotHRuler.cpp
  widgets/FrequencyPlotItem.cpp
  widgets/FrequencyPlotScene.cpp
  widgets/FrequencyPlotView.cpp
  widgets/FrequencyPlotVRuler.cpp
  widgets/MiddleClickTabBar.cpp
  widgets/MiddleClickTabWidget.cpp
  widgets/ResizeCorner.cpp
  widgets/ResizingLineEdit.cpp
  guis/PreampFilterGUI.cpp
  guis/PreampFilterGUIFactory.cpp
  guis/CommentFilterGUI.cpp
  guis/CommentFilterGUIFactory.cpp
  guis/DeviceFilterGUI.cpp
  guis/DeviceFilterGUIDialog.cpp
  guis/DeviceFilterGUIFactory.cpp
  guis/ChannelFilterGUI.cpp
  guis/ChannelFilterGUIChannelItem.cpp
  guis/ChannelFilterGUIDialog.cpp
  guis/ChannelFilterGUIFactory.cpp
  guis/ChannelFilterGUIScene.cpp
  guis/BiQuadFilterGUI.cpp
  guis/BiQuadFilterGUIFactory.cpp
  guis/CopyFilterGUI.cpp
  guis/CopyFilterGUIChannelItem.cpp
  guis/CopyFilterGUIConnectionItem.cpp
  guis/CopyFilterGUIFactory.cpp
  guis/CopyFilterGUIForm.cpp
  guis/CopyFilterGUIRow.cpp
  guis/CopyFilterGUIScene.cpp
  guis/DelayFilterGUI.cpp
  guis/DelayFilterGUIFactory.cpp
  guis/GraphicEQFilterGUI.cpp
  guis/GraphicEQFilterGUIFactory.cpp
  guis/GraphicEQFilterGUIItem.cpp
  guis/GraphicEQFilterGUIScene.cpp
  guis/GraphicEQFilterGUIView.cpp
  guis/IncludeFilterGUI.cpp
  guis/IncludeFilterGUIFactory.cpp
  guis/ConvolutionFilterGUI.cpp
  guis/ConvolutionFilterGUIFactory.cpp
  guis/StageFilterGUI.cpp
  guis/StageFilterGUIFactory.cpp
  guis/ExpressionFilterGUIFactory.cpp
  guis/VSTPluginFilterGUI.cpp
  guis/VSTPluginFilterGUIDialog.cpp
  guis/VSTPluginFilterGUIFactory.cpp
)

if(WIN32)
  list(APPEND EDITOR_SOURCES
    ../AbstractAPOInfo.cpp
    ../DeviceAPOInfo.cpp
    ../VoicemeeterAPOInfo.cpp
    ../helpers/RegistryHelper.cpp
    guis/LoudnessCorrectionFilterGUI.cpp
    guis/LoudnessCorrectionFilterGUIDialog.cpp
    guis/LoudnessCorrectionFilterGUIFactory.cpp
  )
else()
  list(APPEND EDITOR_SOURCES
    ../AbstractAPOInfo.cpp
    helpers/LinuxDeviceInfo.cpp
  )
endif()

set(EDITOR_FORMS
  FilterTableRow.ui
  MainWindow.ui
  guis/BiQuadFilterGUI.ui
  guis/ChannelFilterGUIDialog.ui
  guis/ChannelFilterGUI.ui
  guis/CommentFilterGUI.ui
  guis/ConvolutionFilterGUI.ui
  guis/CopyFilterGUIRow.ui
  guis/CopyFilterGUI.ui
  guis/DelayFilterGUI.ui
  guis/DeviceFilterGUIDialog.ui
  guis/DeviceFilterGUI.ui
  guis/GraphicEQFilterGUI.ui
  guis/IncludeFilterGUI.ui
  guis/PreampFilterGUI.ui
  guis/StageFilterGUI.ui
  guis/VSTPluginFilterGUIDialog.ui
  guis/VSTPluginFilterGUI.ui
)

if(WIN32)
  list(APPEND EDITOR_FORMS
    guis/LoudnessCorrectionFilterGUIDialog.ui
    guis/LoudnessCorrectionFilterGUI.ui
  )
endif()

add_executable(eqapo-editor
  ${EDITOR_SOURCES}
  ${EDITOR_FORMS}
  Editor.qrc
)

# The .pro build uses stable.h as a precompiled header; sources rely on it for
# their common Qt and standard library includes. Keep that behaviour under
# CMake so the Windows and Linux sources stay identical.
target_precompile_headers(eqapo-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stable.h)

target_include_directories(eqapo-editor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/guis
  ${CMAKE_CURRENT_SOURCE_DIR}/widgets
  ${CMAKE_CURRENT_SOURCE_DIR}/helpers
)

target_link_libraries(eqapo-editor PRIVATE
  eqapo_core
  Qt6::Widgets
)

if(NOT WIN32)
  # Shared headers use Windows-isms (__forceinline, HWND, HMODULE, the
  # KSAUDIO_SPEAKER_* masks) that the moc-generated translation units do not
  # reach through stdafx.h. Force-include the compatibility shim everywhere.
  target_compile_options(eqapo-editor PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/../linux/wincompat.h
  )
endif()

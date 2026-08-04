cmake_minimum_required(VERSION 3.16)

# =====================================================================
# 地图生成器 - 统一构建脚本
# 通过 CMake 选项切换 CLI / GUI 两种产物,核心算法代码共用一套
#
# 用法:
#   CLI 版 (默认,仅依赖 zlib):
#       cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Release
#       cmake --build build-cli
#
#   GUI 版 (依赖 Qt6 Widgets + Concurrent):
#       cmake -S . -B build-gui -DBUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
#       cmake --build build-gui
# =====================================================================

option(BUILD_GUI "构建带 Qt6 界面的 GUI 版本 (OFF=命令行版)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(BUILD_GUI)
    project(worldgen_gui LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
else()
    project(worldgen LANGUAGES C)
endif()

# ---------- 核心算法 (两套产物共用) ----------
set(WORLDGEN_CORE_SOURCES
    src/core/worldgen_core.c
    src/core/worldgen_core.h
)

# zlib (CLI/GUI 都需要,用于输出 PNG)
find_package(ZLIB REQUIRED)

# ---------- 目标产物 ----------
if(BUILD_GUI)
    # ===== GUI 版: Qt6 应用 =====

    # -- macOS: 自动对齐 Qt 的 CPU 架构,避免 x86_64 ↔ arm64 链接失败 --
    # 常见症状: ld: symbol(s) not found for architecture x86_64 + _qt_version_tag_6_x
    # 注意: 用 "NOT CMAKE_OSX_ARCHITECTURES" 同时覆盖 "未定义" 与 "空字符串" 两种情况
    if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
        find_package(Qt6 QUIET COMPONENTS Widgets)
        set(_qt6_candidate "")
        if(Qt6Widgets_FOUND)
            # 候选 1: IMPORTED_LOCATION_<CONFIG> (Release 优先)
            foreach(_cfg IN ITEMS RELEASE RELWITHDEBINFO MINSIZEREL "")
                get_target_property(_loc Qt6::Widgets IMPORTED_LOCATION_${_cfg})
                if(_loc AND EXISTS "${_loc}")
                    set(_qt6_candidate "${_loc}")
                    break()
                endif()
            endforeach()
            # 候选 2: 通用 IMPORTED_LOCATION
            if(NOT _qt6_candidate)
                get_target_property(_loc Qt6::Widgets IMPORTED_LOCATION)
                if(_loc AND EXISTS "${_loc}")
                    set(_qt6_candidate "${_loc}")
                endif()
            endif()
            # 候选 3: 从 Qt6_DIR / qmake 推导 (brew 布局: <prefix>/lib/QtWidgets.framework/QtWidgets)
            if(NOT _qt6_candidate)
                get_target_property(_qt6_include Qt6::Widgets INTERFACE_INCLUDE_DIRECTORIES)
                if(_qt6_include)
                    foreach(_d IN LISTS _qt6_include)
                        if(_d MATCHES "^(.+)/lib/QtWidgets\\.framework/Headers$")
                            set(_guess "${CMAKE_MATCH_1}/lib/QtWidgets.framework/QtWidgets")
                            if(EXISTS "${_guess}")
                                set(_qt6_candidate "${_guess}")
                                break()
                            endif()
                        endif()
                    endforeach()
                endif()
            endif()
        endif()

        if(_qt6_candidate)
            execute_process(
                COMMAND lipo -archs "${_qt6_candidate}"
                OUTPUT_VARIABLE _qt6_archs
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _lipo_rc
            )
            if(_lipo_rc EQUAL 0 AND _qt6_archs)
                string(REPLACE " " ";" _qt6_arch_list "${_qt6_archs}")
                list(FIND _qt6_arch_list "arm64" _arm64_idx)
                if(_arm64_idx GREATER_EQUAL 0)
                    set(_detected_arch "arm64")
                else()
                    list(GET _qt6_arch_list 0 _detected_arch)
                endif()
                set(CMAKE_OSX_ARCHITECTURES "${_detected_arch}" CACHE STRING "macOS target architectures (auto-aligned to Qt)" FORCE)
                message(STATUS "[worldgen] 检测到 Qt6 库架构: ${_qt6_archs} -> 对齐 CMAKE_OSX_ARCHITECTURES=${_detected_arch}")
            endif()
        else()
            # 兜底: Apple Silicon 机器上没有任何可探测线索时,默认优先 arm64
            if(EXISTS "/proc/self/cmdline")
                # Linux, 不是 macOS, 不会走到这
            elseif(EXISTS "/opt/homebrew" AND IS_DIRECTORY "/opt/homebrew")
                set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architectures (fallback: arm64 on Apple Silicon)" FORCE)
                message(STATUS "[worldgen] 未找到 Qt6 库路径,按 /opt/homebrew 存在推断为 Apple Silicon -> CMAKE_OSX_ARCHITECTURES=arm64")
            endif()
        endif()
    endif()

    find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
    qt_standard_project_setup()

    # 打印诊断信息,便于排查架构/版本不匹配
    message(STATUS "[worldgen] 模式: GUI (BUILD_GUI=ON)")
    message(STATUS "[worldgen] Qt6 版本: ${Qt6_VERSION}")
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        message(STATUS "[worldgen] macOS 目标架构: ${CMAKE_OSX_ARCHITECTURES}")
    elseif(APPLE)
        message(STATUS "[worldgen] macOS 目标架构: (空/未设置 - 将使用编译器默认,通常为 x86_64 或当前机器架构)")
    endif()

    # Qt 资源 (QSS 样式表 + 窗口图标) 编译进二进制,避免运行时依赖工作目录
    qt_add_resources(WORLDGEN_QRC src/gui/resources.qrc)

    add_executable(worldgen_gui
        src/gui/main.cpp
        src/gui/mainwindow.cpp
        src/gui/mainwindow.h
        ${WORLDGEN_CORE_SOURCES}
        ${WORLDGEN_QRC}
    )

    target_include_directories(worldgen_gui PRIVATE src/core src/gui)

    # macOS: 打包为 .app 应用包 + 图标
    set(WORLDGEN_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/images/world-generator-icon.icns")
    if(EXISTS "${WORLDGEN_MACOS_ICON}")
        target_sources(worldgen_gui PRIVATE "${WORLDGEN_MACOS_ICON}")
        set_source_files_properties("${WORLDGEN_MACOS_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()
    set_target_properties(worldgen_gui PROPERTIES
        OUTPUT_NAME "地图生成器"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.crossdark.worldgen"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "地图生成器"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_ICON_FILE "world-generator-icon.icns"
    )

    # Windows: 无控制台窗口的 GUI 程序
    if(WIN32)
        set_target_properties(worldgen_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    target_link_libraries(worldgen_gui PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        ZLIB::ZLIB
    )
    if(UNIX)
        target_link_libraries(worldgen_gui PRIVATE m)
    endif()

else()
    # ===== CLI 版: 纯 C 命令行程序 =====
    message(STATUS "[worldgen] 模式: CLI (BUILD_GUI=OFF)")

    add_executable(worldgen
        src/cli/main.c
        ${WORLDGEN_CORE_SOURCES}
    )

    target_include_directories(worldgen PRIVATE src/core src/cli)
    set_target_properties(worldgen PROPERTIES OUTPUT_NAME "地图生成器")
    target_link_libraries(worldgen PRIVATE ZLIB::ZLIB)
    if(UNIX)
        target_link_libraries(worldgen PRIVATE m)
    endif()
endif()

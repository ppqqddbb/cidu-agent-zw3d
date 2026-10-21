cmake_minimum_required(VERSION 3.20)

project(CiduZW3DPlugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CIDU_ZW3D_WITH_REAL_SDK "Build against the real ZW3D SDK" OFF)
option(CIDU_ZW3D_WITH_WEBVIEW2 "Build the agent panel with embedded WebView2" ON)
set(CIDU_ZW3D_INSTALL_DIR "" CACHE PATH "ZW3D installation directory")
set(CIDU_ZW3D_ZRC_EXE "" CACHE FILEPATH "ZW3D resource compiler")
set(CIDU_ZW3D_WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/package/build/native" CACHE PATH "Microsoft.Web.WebView2 native SDK directory")

# 在 Windows 上自动探测常见的中望 3D 2026 安装路径，减少手动传参。
if(WIN32 AND NOT CIDU_ZW3D_INSTALL_DIR)
    set(_cidu_zw3d_default_install "D:/Program Files/ZWSOFT/ZW3D 2026")
    if(EXISTS "${_cidu_zw3d_default_install}/ZW3D.lib")
        set(CIDU_ZW3D_INSTALL_DIR "${_cidu_zw3d_default_install}" CACHE PATH "ZW3D installation directory" FORCE)
    endif()
endif()

add_library(cidu_zw3d_shared STATIC
    src/shared/cidu_client.cpp
    src/shared/json_protocol.cpp
    src/shared/model_parameters.cpp
    src/shared/plugin_config.cpp
    src/shared/zw3d_host.cpp
)

set_target_properties(cidu_zw3d_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cidu_zw3d_shared PUBLIC include)

if(WIN32)
    target_link_libraries(cidu_zw3d_shared PUBLIC winhttp ole32)
endif()

if(CIDU_ZW3D_WITH_REAL_SDK)
    if(NOT CIDU_ZW3D_INSTALL_DIR)
        message(FATAL_ERROR "Set CIDU_ZW3D_INSTALL_DIR to the ZW3D installation directory.")
    endif()
    target_include_directories(cidu_zw3d_shared PUBLIC
        "${CIDU_ZW3D_INSTALL_DIR}/api/inc"
    )
    target_link_libraries(cidu_zw3d_shared PUBLIC
        "${CIDU_ZW3D_INSTALL_DIR}/ZW3D.lib"
    )
    target_compile_definitions(cidu_zw3d_shared PUBLIC CIDU_ZW3D_WITH_REAL_SDK)
    if(NOT CIDU_ZW3D_ZRC_EXE AND EXISTS "${CIDU_ZW3D_INSTALL_DIR}/zrc.exe")
        set(CIDU_ZW3D_ZRC_EXE "${CIDU_ZW3D_INSTALL_DIR}/zrc.exe" CACHE FILEPATH "ZW3D resource compiler" FORCE)
    endif()
else()
    # 没有真实 SDK 时使用 stub，方便在普通开发环境编译和跑 CLI。
    target_compile_definitions(cidu_zw3d_shared PUBLIC CIDU_ZW3D_USE_STUB_SDK)
endif()

# Agent 插件的 Ribbon 按钮、右侧栏图标、窗体和布局资源会被 zrc.exe 打包成 .zrc。
set(CIDU_ZW3D_AGENT_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/agent_plugin")
set(CIDU_ZW3D_AGENT_RESOURCE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/ZW3DAgentPlugin.zrc")
set(CIDU_ZW3D_AGENT_RESOURCE_FILES
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Action/CiduZW3D.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Environment-2/Controls/CiduZW3D.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/ResourcePool/CiduZW3D.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/ResourcePool/RibbonPagesUser.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/ResourcePool/RibbonQuickAccessMenus.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-0-Top/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-1-Root/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-2-Z3/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-3-Sketch/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-4-Sheet/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-5-Packet/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-6-Cam/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-7-Animation/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-8-ExplodedView/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-9-3DSketch/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-10-Part/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-11-ECAD Assembly/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-12-ECAD Part/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-13-Assembly/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-17-Master Layout/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/Environment-19-Cam/LayoutStrategy.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/CiduZW3D/LayoutStrategy-1-Primary_1.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/CiduZW3D/LayoutStrategy-2-Intermediate_default_1.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/CiduZW3D/LayoutStrategy-3-Advanced_1.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Settings/Default/Strategy/CiduZW3D/LayoutStrategy-4-Expert_1.zcui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/Icons/CiduAgent.png"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/forms/CiduZW3DAgentForm.ui"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel.html"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel.css"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_bootstrap.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_chat.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_parameters.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_equipment_mapping.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_parameter_bridge.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_auto_recalculation.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_jiavu.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_events.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_param_table.js"
    "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/vendor/marked.umd.js"
)

add_library(CiduParametricEngine SHARED
    src/parametric_plugin/plugin_entry.cpp
    src/parametric_plugin/parametric_service.cpp
)

target_link_libraries(CiduParametricEngine PRIVATE cidu_zw3d_shared)
target_compile_definitions(CiduParametricEngine PRIVATE CIDU_ZW3D_PLUGIN_BUILD)

add_library(ZW3DAgentPlugin SHARED
    src/agent_plugin/plugin_entry.cpp
    src/agent_plugin/agent_service.cpp
    src/agent_plugin/jiavu_automation.cpp
    src/agent_plugin/parameter_value_bridge.cpp
)

target_link_libraries(ZW3DAgentPlugin PRIVATE cidu_zw3d_shared)
target_compile_definitions(ZW3DAgentPlugin PRIVATE CIDU_ZW3D_PLUGIN_BUILD)
if(WIN32)
    target_link_libraries(ZW3DAgentPlugin PRIVATE comdlg32)
endif()
if(WIN32 AND CIDU_ZW3D_WITH_WEBVIEW2)
    target_compile_definitions(ZW3DAgentPlugin PRIVATE CIDU_ZW3D_WITH_WEBVIEW2)
    target_include_directories(ZW3DAgentPlugin PRIVATE
        "${CIDU_ZW3D_WEBVIEW2_DIR}/include"
    )
    target_link_libraries(ZW3DAgentPlugin PRIVATE
        "${CIDU_ZW3D_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
    )
    add_custom_command(TARGET ZW3DAgentPlugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/vendor"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel.html"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel.css"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel.css"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_bootstrap.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_bootstrap.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_chat.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_chat.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_parameters.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_parameters.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_equipment_mapping.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_equipment_mapping.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_parameter_bridge.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_parameter_bridge.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_auto_recalculation.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_auto_recalculation.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_jiavu.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_jiavu.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_events.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_events.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/agent_panel_param_table.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/agent_panel_param_table.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIDU_ZW3D_AGENT_RESOURCE_DIR}/web/vendor/marked.umd.js"
            "$<TARGET_FILE_DIR:ZW3DAgentPlugin>/web/vendor/marked.umd.js"
        VERBATIM
    )
endif()

if(CIDU_ZW3D_WITH_REAL_SDK AND CIDU_ZW3D_ZRC_EXE)
    # 资源文件改动时重新调用 ZW3D resource compiler，生成给注册表引用的 .zrc。
    add_custom_command(
        OUTPUT "${CIDU_ZW3D_AGENT_RESOURCE_OUTPUT}"
        COMMAND "${CIDU_ZW3D_ZRC_EXE}" "${CIDU_ZW3D_AGENT_RESOURCE_DIR}" -o "${CIDU_ZW3D_AGENT_RESOURCE_OUTPUT}"
        WORKING_DIRECTORY "${CIDU_ZW3D_INSTALL_DIR}"
        DEPENDS ${CIDU_ZW3D_AGENT_RESOURCE_FILES}
        VERBATIM
    )
    add_custom_target(ZW3DAgentPluginResource ALL
        DEPENDS "${CIDU_ZW3D_AGENT_RESOURCE_OUTPUT}"
    )
    add_dependencies(ZW3DAgentPlugin ZW3DAgentPluginResource)
elseif(CIDU_ZW3D_WITH_REAL_SDK)
    message(WARNING "ZW3D resource compiler was not found; ZW3DAgentPlugin.zrc will not be generated.")
endif()

add_executable(cidu_zw3d_cli
    tools/cidu_zw3d_cli.cpp
)

target_link_libraries(cidu_zw3d_cli PRIVATE cidu_zw3d_shared)

include(CTest)
if(BUILD_TESTING)
    add_executable(parameter_value_bridge_tests
        tests/parameter_value_bridge_tests.cpp
        src/agent_plugin/parameter_value_bridge.cpp
    )
    target_include_directories(parameter_value_bridge_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/agent_plugin"
    )
    target_link_libraries(parameter_value_bridge_tests PRIVATE cidu_zw3d_shared)
    add_test(NAME parameter_value_bridge_tests COMMAND parameter_value_bridge_tests)
endif()

set(CIDU_ZW3D_BUILD_TARGETS
    CiduParametricEngine
    ZW3DAgentPlugin
    cidu_zw3d_cli
    cidu_zw3d_shared
)
if(BUILD_TESTING)
    list(APPEND CIDU_ZW3D_BUILD_TARGETS parameter_value_bridge_tests)
endif()

foreach(target IN LISTS CIDU_ZW3D_BUILD_TARGETS)
    if(MSVC)
        # /utf-8 保证中文注释和中文 UI 文本在 MSVC 下按 UTF-8 读取。
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
    endif()
endforeach()

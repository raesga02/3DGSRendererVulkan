cmake_minimum_required(VERSION 3.20)
project(3DGSRendererVulkan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Vulkan is required (SDK must be discoverable, e.g. via $VULKAN_SDK).
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------
# Third-party dependencies (git submodules under external/)
# ---------------------------------------------------------------
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
add_subdirectory(external/glfw)

add_subdirectory(external/vk-bootstrap)
add_subdirectory(external/glm)
add_subdirectory(external/fmt)

# Dear ImGui (no CMakeLists upstream; compiled directly).
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui")
set(IMGUI_CORE_FILES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
)
set(IMGUI_BACKEND_FILES
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)
if(EXISTS "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp")
    list(APPEND IMGUI_BACKEND_FILES "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp")
endif()

# ---------------------------------------------------------------
# OpenXR (optional VR presentation path) - detect before the target
# ---------------------------------------------------------------
# Preferred: point OPENXR_SDK_ROOT at an OpenXR-SDK-Source checkout that has
# already been built (headers in build/include, loader in build/src/loader).
# Falls back to a system OpenXR package if available.
option(ENABLE_OPENXR "Build the OpenXR (VR) presentation path" ON)

set(OPENXR_ENABLED FALSE)
set(OPENXR_SYSTEM_PACKAGE FALSE)

if(ENABLE_OPENXR)
    set(OPENXR_SDK_ROOT "" CACHE PATH "OpenXR-SDK-Source root (with built loader)")

    if(NOT OPENXR_SDK_ROOT AND EXISTS "$ENV{HOME}/Develop/TDT16_XR/openxr-sdk/build/include/openxr/openxr.h")
        set(OPENXR_SDK_ROOT "$ENV{HOME}/Develop/TDT16_XR/openxr-sdk" CACHE PATH "OpenXR-SDK-Source root (with built loader)" FORCE)
    endif()

    if(OPENXR_SDK_ROOT AND EXISTS "${OPENXR_SDK_ROOT}/build/include/openxr/openxr.h")
        set(OPENXR_INCLUDE_DIR "${OPENXR_SDK_ROOT}/build/include")
        find_library(OPENXR_LOADER_LIBRARY NAMES openxr_loader
            HINTS
                "${OPENXR_SDK_ROOT}/build/src/loader"
                "${OPENXR_SDK_ROOT}/build/loader"
        )
        if(OPENXR_LOADER_LIBRARY)
            set(OPENXR_ENABLED TRUE)
            get_filename_component(OPENXR_LOADER_DIR "${OPENXR_LOADER_LIBRARY}" DIRECTORY)
            message(STATUS "OpenXR enabled: ${OPENXR_INCLUDE_DIR} / ${OPENXR_LOADER_LIBRARY}")
        else()
            message(WARNING "OpenXR SDK headers found at ${OPENXR_INCLUDE_DIR} but openxr_loader was not found; VR disabled")
        endif()
    endif()

    if(NOT OPENXR_ENABLED)
        find_package(OpenXR QUIET)
        if(OpenXR_FOUND AND TARGET OpenXR::openxr_loader)
            set(OPENXR_ENABLED TRUE)
            set(OPENXR_SYSTEM_PACKAGE TRUE)
            message(STATUS "OpenXR enabled (system package)")
        else()
            message(STATUS "OpenXR not found; building without the VR presentation path")
        endif()
    endif()
endif()

# ---------------------------------------------------------------
# Application target
# ---------------------------------------------------------------
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

if(NOT OPENXR_ENABLED)
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX "/src/xr/")
endif()

add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
    ${IMGUI_CORE_FILES}
    ${IMGUI_BACKEND_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/external/vma/include"
    "${CMAKE_SOURCE_DIR}/external/stb"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    vk-bootstrap::vk-bootstrap
    glm
    fmt::fmt
)

if(OPENXR_ENABLED)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_OPENXR)
    if(OPENXR_SYSTEM_PACKAGE)
        target_link_libraries(${PROJECT_NAME} PRIVATE OpenXR::openxr_loader)
    else()
        target_include_directories(${PROJECT_NAME} PRIVATE "${OPENXR_INCLUDE_DIR}")
        target_link_libraries(${PROJECT_NAME} PRIVATE "${OPENXR_LOADER_LIBRARY}")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_RPATH "${OPENXR_LOADER_DIR}"
            INSTALL_RPATH "${OPENXR_LOADER_DIR}"
        )
    endif()
endif()

# ---------------------------------------------------------------
# Shader compilation (glslangValidator from the Vulkan SDK)
# ---------------------------------------------------------------
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin"
)

set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BUILD_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_BUILD_DIR})

file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
    "${SHADER_SRC_DIR}/*.vert"
    "${SHADER_SRC_DIR}/*.frag"
    "${SHADER_SRC_DIR}/*.comp"
)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found; shaders will not be compiled.")
endif()

foreach(SHADER ${SHADER_FILES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPIRV "${SHADER_BUILD_DIR}/${FILE_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${FILE_NAME}"
        VERBATIM
    )

    list(APPEND SPIRV_SHADERS ${SPIRV})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(${PROJECT_NAME} shaders)

# ---------------------------------------------------------------
# Runtime assets
# ---------------------------------------------------------------
# Scene models are large and downloaded after configure time, so they are NOT
# copied into the build directory. Instead the executable is told where the
# source assets live, so newly downloaded scenes are picked up immediately.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    SCENE_MODELS_DIR="${CMAKE_SOURCE_DIR}/assets/models"
)

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
# Application target
# ---------------------------------------------------------------
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

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

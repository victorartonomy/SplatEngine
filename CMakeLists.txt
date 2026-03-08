cmake_minimum_required(VERSION 3.20)
project(CustomGameEngine VERSION 1.0 LANGUAGES C CXX)

# Require modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------
include(FetchContent)

# Fetch GLFW directly from the source
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
# Disable GLFW tests and docs to speed up the build
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Fetch GLM (Mathematics library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------
# Dear ImGui (docking branch)
# ---------------------------------------------------------
# ImGui has no CMakeLists.txt, so we fetch the source and build it manually.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# ImGui's GLFW backend needs GLFW headers; it uses its own minimal GL loader internally.
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------
# Native File Dialog Extended (OS-native open/save dialogs)
# ---------------------------------------------------------
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.2.1
)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nfd)

# ---------------------------------------------------------
# Project Targets
# ---------------------------------------------------------

# Compile GLAD as a static library
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

# Define your engine executable
add_executable(Engine 
    src/main.cpp
    src/ComputeShader.cpp
    src/COFFParser.cpp
    src/GPUBuffer.cpp
    src/Camera.cpp
    src/TileRasterizer.cpp
)

# Include directories for the Engine
target_include_directories(Engine PRIVATE include)

# Link the libraries to your Engine
target_link_libraries(Engine PRIVATE
    glfw
    glm
    glad
    imgui
    nfd
)

# ---------------------------------------------------------
# Copy Shaders to Build Directory
# ---------------------------------------------------------
# This ensures shaders are accessible whether running from VS, command line, or deployed
add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:Engine>/shaders
    COMMENT "Copying shaders to build directory"
)

# ---------------------------------------------------------
# Copy Models to Build Directory
# ---------------------------------------------------------
add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/models
        $<TARGET_FILE_DIR:Engine>/models
    COMMENT "Copying models to build directory"
)

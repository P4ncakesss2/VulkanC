cmake_minimum_required(VERSION 4.2)
project(VulkanC C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

foreach(OUTPUT_TYPE RUNTIME ARCHIVE LIBRARY)
    set(CMAKE_${OUTPUT_TYPE}_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_${OUTPUT_TYPE}_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
    if(NOT "${OUTPUT_TYPE}" STREQUAL "RUNTIME")
        set(CMAKE_${OUTPUT_TYPE}_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/lib)
        set(CMAKE_${OUTPUT_TYPE}_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
    endif()
endforeach()

include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
        cglm
        GIT_REPOSITORY https://github.com/recp/cglm.git
        GIT_TAG        v0.9.4
)
set(CGLM_BUILD_DOCS OFF CACHE INTERNAL "")
set(CGLM_TEST OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(cglm)

find_package(Vulkan REQUIRED)

add_executable(VulkanC
        main.c
        tinyfiledialogs.c
        tinyfiledialogs.h
)

target_link_libraries(VulkanC PRIVATE
        Vulkan::Vulkan
        glfw
        cglm
)
cmake_minimum_required(VERSION 3.20)
project(VulkanC C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
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

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(vma)

find_package(Vulkan REQUIRED)

# -------------------------------------------------------------------
# Slang Shader Compilation
# -------------------------------------------------------------------

find_program(SLANG_COMPILER slangc REQUIRED)

file(GLOB_RECURSE SLANG_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang"
)

set(STAGED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
set(COMPILED_SPV_FILES "")

foreach(SHADER_PATH ${SLANG_SHADER_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders" ${SHADER_PATH})
    get_filename_component(REL_DIR   "${REL_PATH}" DIRECTORY)
    get_filename_component(SHADER_NAME "${SHADER_PATH}" NAME_WE)

    if(REL_DIR STREQUAL "")
        set(STAGED_SPV  "${STAGED_DIR}/${SHADER_NAME}.spv")
        set(OUTPUT_SPV  "${OUTPUT_DIR}/${SHADER_NAME}.spv")
        set(DISPLAY     "${SHADER_NAME}")
    else()
        set(STAGED_SPV  "${STAGED_DIR}/${REL_DIR}/${SHADER_NAME}.spv")
        set(OUTPUT_SPV  "${OUTPUT_DIR}/${REL_DIR}/${SHADER_NAME}.spv")
        set(DISPLAY     "${REL_DIR}/${SHADER_NAME}")
    endif()

    add_custom_command(
        OUTPUT  ${STAGED_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGED_DIR}/${REL_DIR}"
        COMMAND ${SLANG_COMPILER} ${SHADER_PATH} -target spirv -fvk-use-entrypoint-name -o ${STAGED_SPV}
        DEPENDS ${SHADER_PATH}
        COMMENT "Compiling ${DISPLAY}.slang"
    )

    add_custom_command(
        OUTPUT  ${OUTPUT_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}/${REL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STAGED_SPV} ${OUTPUT_SPV}
        DEPENDS ${STAGED_SPV}
        COMMENT "Copying ${DISPLAY}.spv to bin/shaders"
    )

    list(APPEND COMPILED_SPV_FILES ${OUTPUT_SPV})
endforeach()

add_custom_target(CompileShaders DEPENDS ${COMPILED_SPV_FILES})

# -------------------------------------------------------------------
# Target Configuration
# -------------------------------------------------------------------

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(VulkanC ${SRC_FILES})
target_include_directories(VulkanC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_dependencies(VulkanC CompileShaders)

target_link_libraries(VulkanC PRIVATE
        Vulkan::Vulkan
        glfw
        cglm
        GPUOpen::VulkanMemoryAllocator
)

add_custom_command(TARGET VulkanC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:VulkanC>/assets"
    COMMENT "Copying assets"
)
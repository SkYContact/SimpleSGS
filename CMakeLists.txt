cmake_minimum_required(VERSION 3.16)
project(sgs1v1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SGS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(sgs1v1 ${SGS_SOURCES})
target_include_directories(sgs1v1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (MSVC)
    target_compile_options(sgs1v1 PRIVATE /utf-8 /W3)
else()
    target_compile_options(sgs1v1 PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# 把牌堆数据复制到可执行文件旁边（运行时按相对路径查找）
add_custom_command(TARGET sgs1v1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data
            $<TARGET_FILE_DIR:sgs1v1>/data)

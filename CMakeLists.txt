cmake_minimum_required(VERSION 3.10)
project(adofai_mod LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-O2 -fPIC -Wall -Wno-unused")

# Android NDK 下由 gradle/ndk-build 传入; 本地测试可手动指定
if(ANDROID_ABI)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DANDROID")
endif()

add_library(adofai_mod SHARED
    core/mod_loader.c
    core/hook.c
    jni/jni_entry.c
)

target_include_directories(adofai_mod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/jni
)

# zlib (提供 unzip 所需 deflate; minizip 需自行放入 core/minizip/)
find_library(ZLIB z)
target_link_libraries(adofai_mod log dl ${ZLIB})

# Dobby (需预先编译或放入 third_party/dobby/)
# 若未提供 Dobby, hook.c 中的 do_hook 会被链接器跳过 (弱符号), 仅 mod_loader 可用
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dobby)
    add_subdirectory(third_party/dobby)
    target_link_libraries(adofai_mod dobby)
endif()

install(TARGETS adofai_mod LIBRARY DESTINATION lib/${ANDROID_ABI})

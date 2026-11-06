cmake_minimum_required(VERSION 3.20)
# Build-time tools for the Android (cross) build, compiled for this PC.
project(MegaManXHostTools LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(_root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(mmx_saber_sfx_convert
    "${_root}/tools/saber/mmx_saber_sfx_convert.c"
    "${_root}/tools/saber/mmx_saber_sfx_codec.c"
    "${_root}/tools/saber/third_party/stb_vorbis.c")
target_include_directories(mmx_saber_sfx_convert PRIVATE "${_root}/src/saber" "${_root}/tools/saber")
if(NOT WIN32)
    target_link_libraries(mmx_saber_sfx_convert PRIVATE m)
endif()
# One fixed path for every generator and configuration.
foreach(_cfg "" _RELEASE _DEBUG _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(mmx_saber_sfx_convert PROPERTIES RUNTIME_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

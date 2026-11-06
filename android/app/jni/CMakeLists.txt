cmake_minimum_required(VERSION 3.20)
project(MegaManXSNESRecompAndroid LANGUAGES C CXX)

# The repository's own CMake project builds the game as libmain.so on Android
# (SDLActivity loads it and calls SDL_main). The runner builds libSDL3.so from
# android/SDL3 (SNESRECOMP_SDL3_SOURCE_DIR), the release the Java glue is from.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. mmx)

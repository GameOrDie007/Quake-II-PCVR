# Raised from 3.0: CMake 4.x refuses any minimum below 3.5 outright, and this
# port builds with the CMake bundled in VS Build Tools 2026 (4.3.1). A declared
# minimum affects policy defaults only, not the build itself.
cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

# Enforce "Debug" as standard build type.
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

# CMake project configuration.
project(yquake2)

# Cmake module search path.
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/stuff/cmake/modules ${CMAKE_MODULE_PATH})

if(YQUAKE2LIBS)
	if(CMAKE_CROSSCOMPILING)
		set(CMAKE_FIND_ROOT_PATH ${YQUAKE2LIBS})
	else()
		set(ENV{CMAKE_PREFIX_PATH} ${YQUAKE2LIBS})
	endif()

	set(ENV{OPENALDIR} ${YQUAKE2LIBS})
	set(ENV{SDL2DIR} ${YQUAKE2LIBS})
endif()

# Add extended path for FreeBSD and Homebrew on OS X.
list(APPEND CMAKE_PREFIX_PATH /usr/local)

# MSVC support, backported from yquake2 8.x.
#
# 7.41 predates upstream's MSVC support, and this port needs MSVC because the
# OpenXR loader and the rest of the PCVR build use it. Confined entirely to
# this file - no engine source is touched - because the build system is the one
# part of a port that is always replaced anyway. Team Beef build with the
# Android NDK, so neither of yquake2's own build files is what they used.
if (MSVC)
	add_compile_options(/MP) # parallel build

	# Same warnings upstream silences for MSVC.
	add_compile_options(/wd4244 /wd4305) # possible loss of data / truncation
	add_compile_options(/wd4018) # signed/unsigned mismatch
	add_compile_options(/wd4996) # 'function' declared deprecated (secure CRT)
	add_compile_options(/experimental:external /external:W0) # quiet system headers
else()

# Enforce compiler flags:
#  -Wall                -> More warnings
#  -fno-strict-aliasing -> Quake 2 is far away from strict aliasing
#  -fwrapv              -> Make signed integer overflows defined
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -fno-strict-aliasing -fwrapv")

# -fcommon: GCC 10 flipped its default to -fno-common, which turns a tentative
# definition such as "cvar_t *m_yaw;" appearing in two translation units into
# two real definitions instead of merging them. 7.41 predates that change and
# relies on the old behaviour - m_yaw, m_side, sensitivity, vid_fullscreen and
# others are declared this way - so a modern GCC fails the link with "multiple
# definition of".
#
# Restoring the old default is one flag and leaves every source file untouched,
# which is what a 1:1 port wants. Upstream later fixed the declarations
# properly, but adopting that here would mean importing engine changes from a
# version this port is deliberately not based on.
include(CheckCCompilerFlag)
check_c_compiler_flag("-fcommon" HAVE_FCOMMON)
if(HAVE_FCOMMON)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcommon")
endif()

# Switch of some annoying warnings
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-braces")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	if (CMAKE_CXX_COMPILER_VERSION GREATER 7.99)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format-truncation -Wno-format-overflow")
	endif()
endif()

# Use -O2 as maximum optimization level. -O3 has it's problems with yquake2.
string(REPLACE "-O3" "-O2" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

endif() # MSVC

# Compilation time options.
option(CURL_SUPPORT "cURL support" ON)
option(OPENAL_SUPPORT "OpenAL support" ON)
option(SYSTEMWIDE_SUPPORT "Enable systemwide installation of game assets" OFF)

# These variables will act as our list of include folders and linker flags.
set(yquake2IncludeDirectories)
set(yquake2LinkerDirectories)
set(yquake2LinkerFlags)
set(yquake2ClientLinkerFlags)
set(yquake2ServerLinkerFlags)
set(yquake2OpenGLLinkerFlags)
set(yquake2SDLLinkerFlags)
set(yquake2ZLibLinkerFlags)

# Set directory locations (allowing us to move directories easily)
set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(BACKENDS_SRC_DIR ${SOURCE_DIR}/backends)
set(COMMON_SRC_DIR ${SOURCE_DIR}/common)
set(GAME_SRC_DIR ${SOURCE_DIR}/game)
set(SERVER_SRC_DIR ${SOURCE_DIR}/server)
set(CLIENT_SRC_DIR ${SOURCE_DIR}/client)
set(VR_SRC_DIR ${SOURCE_DIR}/vr)

# OpenXR
#
# MSYS2 ships both the headers and libopenxr_loader.dll.a, so the loader is
# taken from the toolchain rather than built from source. The loader version
# does not have to match the runtime - VDXR, SteamVR and the Oculus runtime are
# all reached through the same loader - but note that vr_surface.c asks for
# apiVersion 1.0 deliberately, because VDXR rejects 1.1.
find_path(OPENXR_INCLUDE_DIR openxr/openxr.h)
find_library(OPENXR_LOADER_LIBRARY NAMES openxr_loader)

if(OPENXR_INCLUDE_DIR AND OPENXR_LOADER_LIBRARY)
	message(STATUS "Found OpenXR loader: ${OPENXR_LOADER_LIBRARY}")
else()
	message(FATAL_ERROR "OpenXR loader not found. On MSYS2 install mingw-w64-x86_64-openxr-loader.")
endif()
set(REF_SRC_DIR ${SOURCE_DIR}/client/refresh)

# Operating system.
set(YQ2OSTYPE "${CMAKE_SYSTEM_NAME}" CACHE STRING "Override operation system type")
add_definitions(-DYQ2OSTYPE="${YQ2OSTYPE}")

# Architecture string.
set(YQ2ARCH "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "Override CPU architecture")

# Lowercase first: on Windows CMAKE_SYSTEM_PROCESSOR is "AMD64", and the
# substitutions below are lowercase-only, so without this ARCH stays "AMD64"
# and none of the per-architecture branches match.
string(TOLOWER "${YQ2ARCH}" ARCH)

string(REGEX REPLACE "amd64" "x86_64" ARCH ${ARCH})
string(REGEX REPLACE "i.86" "i386" ARCH ${ARCH})
string(REGEX REPLACE "^arm.*" "arm" ARCH ${ARCH})

# 7.41's common.h has "#error YQ2ARCH should be defined by the build system",
# so it must come from here. 8.x added an in-code fallback for MSVC instead;
# keeping 7.41's approach means no engine source changes for the sake of the
# build.
add_definitions(-DYQ2ARCH="${ARCH}")

# Systemwide installation of game assets.
if(${SYSTEMWIDE_SUPPORT})
    add_definitions(-DSYSTEMWIDE)
endif()

# We need to pass some options to minizip / unzip.
add_definitions(-DNOUNCRYPT)

if(NOT CMAKE_SYSTEM_NAME MATCHES "Linux" OR NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
	add_definitions(-DIOAPI_NO_64)
endif()

# Required libraries to build the different components of the binaries. Find
# them and add the include/linker directories and flags (in case the package
# manager find it in a weird place).
find_package(SDL2 REQUIRED)
list(APPEND yquake2IncludeDirectories "${SDL2_INCLUDE_DIR}/..")
list(APPEND yquake2SDLLinkerFlags ${SDL2_LIBRARY})

# We need an OpenGL implementation.
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
list(APPEND yquake2IncludeDirectories ${OPENGL_INCLUDE_DIR})
list(APPEND yquake2OpenGLLinkerFlags ${OPENGL_LIBRARIES})

# FreeBSD needs libexecinfo.
if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "FreeBSD")
	find_library(EXECINFO_LIBRARIES execinfo /usr/lib /usr/local/lib)
	list(APPEND yquake2ClientLinkerFlags ${EXECINFO_LIBRARIES})
	list(APPEND yquake2ServerLinkerFlags ${EXECINFO_LIBRARIES})
endif()

# cURL support.
if (${CURL_SUPPORT})
	find_package(CURL REQUIRED)
	add_definitions(-DUSE_CURL)
endif()

# OpenAL support.
if(${OPENAL_SUPPORT})
	find_package(OpenAL)

	if(${OPENAL_FOUND})
		list(APPEND yquake2IncludeDirectories "${OPENAL_INCLUDE_DIR}")
		list(APPEND yquake2ClientLinkerFlags ${OPENAL_LIBRARY})

		if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
			add_definitions(-DUSE_OPENAL -DDEFAULT_OPENAL_DRIVER="openal32.dll")
		elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
			add_definitions(-DUSE_OPENAL -DDEFAULT_OPENAL_DRIVER="libopenal.dylib")
		elseif((${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD") OR (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD"))
			add_definitions(-DUSE_OPENAL -DDEFAULT_OPENAL_DRIVER="libopenal.so")
		else()
			add_definitions(-DUSE_OPENAL -DDEFAULT_OPENAL_DRIVER="libopenal.so.1")
		endif()
	endif()
endif()

# General linker flags.
# MSVC has no libm to link and no libgcc to static-link; both are GCC-isms and
# passing them through would fail the link.
if(MSVC)
	# nothing to add
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	list(APPEND yquake2LinkerFlags "-lm -static-libgcc")
else()
	list(APPEND yquake2LinkerFlags "-lm -rdynamic")
endif()

list(APPEND yquake2LinkerFlags ${CMAKE_DL_LIBS})

# With all of those libraries and user defined paths
# added, lets give them to the compiler and linker.
include_directories(${yquake2IncludeDirectories} ${CMAKE_SOURCE_DIR}/src/client/refresh/gl3/glad/include)
link_directories(${yquake2LinkerDirectories})

# If we're building with gcc for i386 let's define -ffloat-store.
# This helps the old and crappy x87 FPU to produce correct values.
# Would be nice if Clang had something comparable.
if ("${ARCH}" STREQUAL "i386")
	if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffloat-store")
	endif()
endif()

# Force SSE math on x86_64. All sane compilers should do this
# anyway, just to protect us from broken Linux distros.
# -mfpmath is a GCC flag; MSVC uses SSE2 on x64 unconditionally.
if ("${ARCH}" STREQUAL "x86_64" AND NOT MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpmath=sse")
endif()

set(Backends-Generic-Source
	${BACKENDS_SRC_DIR}/generic/misc.c
	)

set(Backends-Unix-Source
	${BACKENDS_SRC_DIR}/unix/main.c
	${BACKENDS_SRC_DIR}/unix/network.c
	${BACKENDS_SRC_DIR}/unix/signalhandler.c
	${BACKENDS_SRC_DIR}/unix/system.c
	${BACKENDS_SRC_DIR}/unix/shared/hunk.c
	)

set(Backends-Windows-Source
	${BACKENDS_SRC_DIR}/windows/icon.rc
	${BACKENDS_SRC_DIR}/windows/main.c
	${BACKENDS_SRC_DIR}/windows/network.c
	${BACKENDS_SRC_DIR}/windows/system.c
	${BACKENDS_SRC_DIR}/windows/shared/hunk.c
	)

set(Backends-Windows-Header
	${BACKENDS_SRC_DIR}/windows/header/resource.h
	)

set(REF-Windows-Source
	${BACKENDS_SRC_DIR}/windows/shared/hunk.c
	)

set(REF-Unix-Source
	${BACKENDS_SRC_DIR}/unix/shared/hunk.c
	)

# Set the nessesary platform specific source
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(Platform-Specific-Source ${Backends-Windows-Source} ${Backends-Windows-Header})
	set(REF-Platform-Specific-Source ${REF-Windows-Source})
else()
	set(Platform-Specific-Source ${Backends-Unix-Source})
	set(REF-Platform-Specific-Source ${REF-Unix-Source})
endif()

set(Game-Source
	${COMMON_SRC_DIR}/shared/flash.c
	${COMMON_SRC_DIR}/shared/rand.c
	${COMMON_SRC_DIR}/shared/shared.c
	${GAME_SRC_DIR}/g_ai.c
	${GAME_SRC_DIR}/g_chase.c
	${GAME_SRC_DIR}/g_cmds.c
	${GAME_SRC_DIR}/g_combat.c
	${GAME_SRC_DIR}/g_func.c
	${GAME_SRC_DIR}/g_items.c
	${GAME_SRC_DIR}/g_main.c
	${GAME_SRC_DIR}/g_misc.c
	${GAME_SRC_DIR}/g_monster.c
	${GAME_SRC_DIR}/g_phys.c
	${GAME_SRC_DIR}/g_spawn.c
	${GAME_SRC_DIR}/g_svcmds.c
	${GAME_SRC_DIR}/g_target.c
	${GAME_SRC_DIR}/g_trigger.c
	${GAME_SRC_DIR}/g_turret.c
	${GAME_SRC_DIR}/g_utils.c
	${GAME_SRC_DIR}/g_weapon.c
	${GAME_SRC_DIR}/monster/berserker/berserker.c
	${GAME_SRC_DIR}/monster/boss2/boss2.c
	${GAME_SRC_DIR}/monster/boss3/boss3.c
	${GAME_SRC_DIR}/monster/boss3/boss31.c
	${GAME_SRC_DIR}/monster/boss3/boss32.c
	${GAME_SRC_DIR}/monster/brain/brain.c
	${GAME_SRC_DIR}/monster/chick/chick.c
	${GAME_SRC_DIR}/monster/flipper/flipper.c
	${GAME_SRC_DIR}/monster/float/float.c
	${GAME_SRC_DIR}/monster/flyer/flyer.c
	${GAME_SRC_DIR}/monster/gladiator/gladiator.c
	${GAME_SRC_DIR}/monster/gunner/gunner.c
	${GAME_SRC_DIR}/monster/hover/hover.c
	${GAME_SRC_DIR}/monster/infantry/infantry.c
	${GAME_SRC_DIR}/monster/insane/insane.c
	${GAME_SRC_DIR}/monster/medic/medic.c
	${GAME_SRC_DIR}/monster/misc/move.c
	${GAME_SRC_DIR}/monster/mutant/mutant.c
	${GAME_SRC_DIR}/monster/parasite/parasite.c
	${GAME_SRC_DIR}/monster/soldier/soldier.c
	${GAME_SRC_DIR}/monster/supertank/supertank.c
	${GAME_SRC_DIR}/monster/tank/tank.c
	${GAME_SRC_DIR}/player/client.c
	${GAME_SRC_DIR}/player/hud.c
	${GAME_SRC_DIR}/player/trail.c
	${GAME_SRC_DIR}/player/view.c
	${GAME_SRC_DIR}/player/weapon.c
	${GAME_SRC_DIR}/savegame/savegame.c
	)

set(Game-Header
	${GAME_SRC_DIR}/header/game.h
	${GAME_SRC_DIR}/header/local.h
	${GAME_SRC_DIR}/monster/berserker/berserker.h
	${GAME_SRC_DIR}/monster/boss2/boss2.h
	${GAME_SRC_DIR}/monster/boss3/boss31.h
	${GAME_SRC_DIR}/monster/boss3/boss32.h
	${GAME_SRC_DIR}/monster/brain/brain.h
	${GAME_SRC_DIR}/monster/chick/chick.h
	${GAME_SRC_DIR}/monster/flipper/flipper.h
	${GAME_SRC_DIR}/monster/float/float.h
	${GAME_SRC_DIR}/monster/flyer/flyer.h
	${GAME_SRC_DIR}/monster/gladiator/gladiator.h
	${GAME_SRC_DIR}/monster/gunner/gunner.h
	${GAME_SRC_DIR}/monster/hover/hover.h
	${GAME_SRC_DIR}/monster/infantry/infantry.h
	${GAME_SRC_DIR}/monster/insane/insane.h
	${GAME_SRC_DIR}/monster/medic/medic.h
	${GAME_SRC_DIR}/monster/misc/player.h
	${GAME_SRC_DIR}/monster/mutant/mutant.h
	${GAME_SRC_DIR}/monster/parasite/parasite.h
	${GAME_SRC_DIR}/monster/soldier/soldier.h
	${GAME_SRC_DIR}/monster/supertank/supertank.h
	${GAME_SRC_DIR}/monster/tank/tank.h
	${GAME_SRC_DIR}/savegame/tables/clientfields.h
	${GAME_SRC_DIR}/savegame/tables/fields.h
	${GAME_SRC_DIR}/savegame/tables/gamefunc_decs.h
	${GAME_SRC_DIR}/savegame/tables/gamefunc_list.h
	${GAME_SRC_DIR}/savegame/tables/gamemmove_decs.h
	${GAME_SRC_DIR}/savegame/tables/gamemmove_list.h
	${GAME_SRC_DIR}/savegame/tables/levelfields.h
	)

set(Client-Source
	${CLIENT_SRC_DIR}/cl_cin.c
	${CLIENT_SRC_DIR}/cl_console.c
	${CLIENT_SRC_DIR}/cl_download.c
	${CLIENT_SRC_DIR}/cl_effects.c
	${CLIENT_SRC_DIR}/cl_entities.c
	${CLIENT_SRC_DIR}/cl_input.c
	${CLIENT_SRC_DIR}/cl_inventory.c
	${CLIENT_SRC_DIR}/cl_keyboard.c
	${CLIENT_SRC_DIR}/cl_lights.c
	${CLIENT_SRC_DIR}/cl_main.c
	${CLIENT_SRC_DIR}/cl_network.c
	${CLIENT_SRC_DIR}/cl_parse.c
	${CLIENT_SRC_DIR}/cl_particles.c
	${CLIENT_SRC_DIR}/cl_prediction.c
	${CLIENT_SRC_DIR}/cl_screen.c
	${CLIENT_SRC_DIR}/cl_tempentities.c
	${CLIENT_SRC_DIR}/cl_view.c
	${CLIENT_SRC_DIR}/curl/download.c
	${CLIENT_SRC_DIR}/curl/qcurl.c
	${CLIENT_SRC_DIR}/input/sdl.c
	${CLIENT_SRC_DIR}/menu/menu.c
	${CLIENT_SRC_DIR}/menu/qmenu.c
	${CLIENT_SRC_DIR}/menu/videomenu.c
	${CLIENT_SRC_DIR}/sound/ogg.c
	${CLIENT_SRC_DIR}/sound/openal.c
	${CLIENT_SRC_DIR}/sound/qal.c
	${CLIENT_SRC_DIR}/sound/sdl.c
	${CLIENT_SRC_DIR}/sound/sound.c
	${CLIENT_SRC_DIR}/sound/wave.c
	${CLIENT_SRC_DIR}/vid/glimp_sdl.c
	${CLIENT_SRC_DIR}/vid/vid.c
	${COMMON_SRC_DIR}/argproc.c
	${COMMON_SRC_DIR}/clientserver.c
	${COMMON_SRC_DIR}/collision.c
	${COMMON_SRC_DIR}/crc.c
	${COMMON_SRC_DIR}/cmdparser.c
	${COMMON_SRC_DIR}/cvar.c
	${COMMON_SRC_DIR}/filesystem.c
	${COMMON_SRC_DIR}/glob.c
	${COMMON_SRC_DIR}/md4.c
	${COMMON_SRC_DIR}/movemsg.c
	${COMMON_SRC_DIR}/frame.c
	${COMMON_SRC_DIR}/netchan.c
	${COMMON_SRC_DIR}/pmove.c
	${COMMON_SRC_DIR}/szone.c
	${COMMON_SRC_DIR}/zone.c
	${COMMON_SRC_DIR}/shared/flash.c
	${COMMON_SRC_DIR}/shared/rand.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/unzip/ioapi.c
	${COMMON_SRC_DIR}/unzip/miniz.c
	${COMMON_SRC_DIR}/unzip/unzip.c
	${SERVER_SRC_DIR}/sv_cmd.c
	${SERVER_SRC_DIR}/sv_conless.c
	${SERVER_SRC_DIR}/sv_entities.c
	${SERVER_SRC_DIR}/sv_game.c
	${SERVER_SRC_DIR}/sv_init.c
	${SERVER_SRC_DIR}/sv_main.c
	${SERVER_SRC_DIR}/sv_save.c
	${SERVER_SRC_DIR}/sv_send.c
	${SERVER_SRC_DIR}/sv_user.c
	${SERVER_SRC_DIR}/sv_world.c

	# Team Beef's VR layer, ported unchanged from Quake2Quest. It lives outside
	# quake2/src in their Android project, so it is absent from the engine diff
	# and has to be listed here explicitly.
	${VR_SRC_DIR}/vr_platform.c
	${VR_SRC_DIR}/vr_surface.c
	${VR_SRC_DIR}/teambeef/VrInputCommon.c
	${VR_SRC_DIR}/teambeef/VrInputDefault.c
	${VR_SRC_DIR}/teambeef/mathlib.c
	${VR_SRC_DIR}/teambeef/matrixlib.c
	)

set(Client-Header
	${CLIENT_SRC_DIR}/header/client.h
	${CLIENT_SRC_DIR}/header/console.h
	${CLIENT_SRC_DIR}/header/keyboard.h
	${CLIENT_SRC_DIR}/header/screen.h
	${CLIENT_SRC_DIR}/curl/header/download.h
	${CLIENT_SRC_DIR}/curl/header/qcurl.h
	${CLIENT_SRC_DIR}/input/header/input.h
	${CLIENT_SRC_DIR}/menu/header/qmenu.h
	${CLIENT_SRC_DIR}/sound/header/local.h
	${CLIENT_SRC_DIR}/sound/header/qal.h
	${CLIENT_SRC_DIR}/sound/header/sound.h
	${CLIENT_SRC_DIR}/sound/header/stb_vorbis.h
	${CLIENT_SRC_DIR}/sound/header/vorbis.h
	${CLIENT_SRC_DIR}/vid/header/ref.h
	${CLIENT_SRC_DIR}/vid/header/stb_image_write.h
	${CLIENT_SRC_DIR}/vid/header/vid.h
	${COMMON_SRC_DIR}/header/common.h
	${COMMON_SRC_DIR}/header/crc.h
	${COMMON_SRC_DIR}/header/files.h
	${COMMON_SRC_DIR}/header/glob.h
	${COMMON_SRC_DIR}/header/shared.h
	${COMMON_SRC_DIR}/header/zone.h
	${COMMON_SRC_DIR}/unzip/ioapi.h
	${COMMON_SRC_DIR}/unzip/miniz.h
	${COMMON_SRC_DIR}/unzip/minizconf.h
	${COMMON_SRC_DIR}/unzip/unzip.h
	${SERVER_SRC_DIR}/header/server.h
	)

set(Server-Source
	${COMMON_SRC_DIR}/argproc.c
	${COMMON_SRC_DIR}/clientserver.c
	${COMMON_SRC_DIR}/collision.c
	${COMMON_SRC_DIR}/crc.c
	${COMMON_SRC_DIR}/cmdparser.c
	${COMMON_SRC_DIR}/cvar.c
	${COMMON_SRC_DIR}/filesystem.c
	${COMMON_SRC_DIR}/glob.c
	${COMMON_SRC_DIR}/md4.c
	${COMMON_SRC_DIR}/frame.c
	${COMMON_SRC_DIR}/movemsg.c
	${COMMON_SRC_DIR}/netchan.c
	${COMMON_SRC_DIR}/pmove.c
	${COMMON_SRC_DIR}/szone.c
	${COMMON_SRC_DIR}/zone.c
	${COMMON_SRC_DIR}/shared/rand.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/unzip/ioapi.c
	${COMMON_SRC_DIR}/unzip/miniz.c
	${COMMON_SRC_DIR}/unzip/unzip.c
	${SERVER_SRC_DIR}/sv_cmd.c
	${SERVER_SRC_DIR}/sv_conless.c
	${SERVER_SRC_DIR}/sv_entities.c
	${SERVER_SRC_DIR}/sv_game.c
	${SERVER_SRC_DIR}/sv_init.c
	${SERVER_SRC_DIR}/sv_main.c
	${SERVER_SRC_DIR}/sv_save.c
	${SERVER_SRC_DIR}/sv_send.c
	${SERVER_SRC_DIR}/sv_user.c
	${SERVER_SRC_DIR}/sv_world.c
	)

set(Server-Header
	${COMMON_SRC_DIR}/header/common.h
	${COMMON_SRC_DIR}/header/crc.h
	${COMMON_SRC_DIR}/header/files.h
	${COMMON_SRC_DIR}/header/glob.h
	${COMMON_SRC_DIR}/header/shared.h
	${COMMON_SRC_DIR}/header/zone.h
	${COMMON_SRC_DIR}/unzip/ioapi.h
	${COMMON_SRC_DIR}/unzip/miniz.h
	${COMMON_SRC_DIR}/unzip/minizconf.h
	${COMMON_SRC_DIR}/unzip/unzip.h
	${SERVER_SRC_DIR}/header/server.h
	)

set(GL1-Source
	${REF_SRC_DIR}/gl1/qgl.c
	${REF_SRC_DIR}/gl1/gl1_draw.c
	${REF_SRC_DIR}/gl1/gl1_image.c
	${REF_SRC_DIR}/gl1/gl1_light.c
	${REF_SRC_DIR}/gl1/gl1_lightmap.c
	${REF_SRC_DIR}/gl1/gl1_main.c
	${REF_SRC_DIR}/gl1/gl1_mesh.c
	${REF_SRC_DIR}/gl1/gl1_misc.c
	${REF_SRC_DIR}/gl1/gl1_model.c
	${REF_SRC_DIR}/gl1/gl1_scrap.c
	${REF_SRC_DIR}/gl1/gl1_surf.c
	${REF_SRC_DIR}/gl1/gl1_warp.c
	${REF_SRC_DIR}/gl1/gl1_sdl.c
	${REF_SRC_DIR}/gl1/gl1_md2.c
	${REF_SRC_DIR}/gl1/gl1_sp2.c
	${REF_SRC_DIR}/files/pcx.c
	${REF_SRC_DIR}/files/stb.c
	${REF_SRC_DIR}/files/wal.c
	${REF_SRC_DIR}/files/pvs.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/md4.c
	)

set(GL1-Header
	${REF_SRC_DIR}/ref_shared.h
	${REF_SRC_DIR}/constants/anorms.h
	${REF_SRC_DIR}/constants/anormtab.h
	${REF_SRC_DIR}/constants/warpsin.h
	${REF_SRC_DIR}/files/stb_image.h
	${REF_SRC_DIR}/gl1/header/local.h
	${REF_SRC_DIR}/gl1/header/model.h
	${REF_SRC_DIR}/gl1/header/qgl.h
	${COMMON_SRC_DIR}/header/shared.h
	)

set(GL3-Source
	${REF_SRC_DIR}/gl3/gl3_draw.c
	${REF_SRC_DIR}/gl3/gl3_image.c
	${REF_SRC_DIR}/gl3/gl3_light.c
	${REF_SRC_DIR}/gl3/gl3_lightmap.c
	${REF_SRC_DIR}/gl3/gl3_main.c
	${REF_SRC_DIR}/gl3/gl3_mesh.c
	${REF_SRC_DIR}/gl3/gl3_misc.c
	${REF_SRC_DIR}/gl3/gl3_model.c
	${REF_SRC_DIR}/gl3/gl3_sdl.c
	${REF_SRC_DIR}/gl3/gl3_surf.c
	${REF_SRC_DIR}/gl3/gl3_warp.c
	${REF_SRC_DIR}/gl3/gl3_shaders.c
	${REF_SRC_DIR}/gl3/gl3_md2.c
	${REF_SRC_DIR}/gl3/gl3_sp2.c
	${REF_SRC_DIR}/gl3/glad/src/glad.c
	${REF_SRC_DIR}/files/pcx.c
	${REF_SRC_DIR}/files/stb.c
	${REF_SRC_DIR}/files/wal.c
	${REF_SRC_DIR}/files/pvs.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/md4.c
	)

set(GL3-Header
	${REF_SRC_DIR}/ref_shared.h
	${REF_SRC_DIR}/constants/anorms.h
	${REF_SRC_DIR}/constants/anormtab.h
	${REF_SRC_DIR}/constants/warpsin.h
	${REF_SRC_DIR}/files/stb_image.h
	${REF_SRC_DIR}/gl3/glad/include/glad/glad.h
	${REF_SRC_DIR}/gl3/glad/include/KHR/khrplatform.h
	${REF_SRC_DIR}/gl3/header/DG_dynarr.h
	${REF_SRC_DIR}/gl3/header/HandmadeMath.h
	${REF_SRC_DIR}/gl3/header/local.h
	${REF_SRC_DIR}/gl3/header/model.h
	${COMMON_SRC_DIR}/header/shared.h
	)

set(SOFT-Source
	${REF_SRC_DIR}/soft/sw_aclip.c
	${REF_SRC_DIR}/soft/sw_alias.c
	${REF_SRC_DIR}/soft/sw_bsp.c
	${REF_SRC_DIR}/soft/sw_draw.c
	${REF_SRC_DIR}/soft/sw_edge.c
	${REF_SRC_DIR}/soft/sw_image.c
	${REF_SRC_DIR}/soft/sw_light.c
	${REF_SRC_DIR}/soft/sw_main.c
	${REF_SRC_DIR}/soft/sw_misc.c
	${REF_SRC_DIR}/soft/sw_model.c
	${REF_SRC_DIR}/soft/sw_part.c
	${REF_SRC_DIR}/soft/sw_poly.c
	${REF_SRC_DIR}/soft/sw_polyset.c
	${REF_SRC_DIR}/soft/sw_rast.c
	${REF_SRC_DIR}/soft/sw_scan.c
	${REF_SRC_DIR}/soft/sw_sprite.c
	${REF_SRC_DIR}/soft/sw_surf.c
	${REF_SRC_DIR}/files/pcx.c
	${REF_SRC_DIR}/files/stb.c
	${REF_SRC_DIR}/files/wal.c
	${REF_SRC_DIR}/files/pvs.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/md4.c
	)

set(SOFT-Header
	${REF_SRC_DIR}/ref_shared.h
	${REF_SRC_DIR}/files/stb_image.h
	${REF_SRC_DIR}/files/stb_image_resize.h
	${REF_SRC_DIR}/soft/header/local.h
	${REF_SRC_DIR}/soft/header/model.h
	${COMMON_SRC_DIR}/header/shared.h
	)

# Wrapper for the Windows binary
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(Wrapper-Source
	src/win-wrapper/wrapper.c
	${BACKENDS_SRC_DIR}/windows/icon.rc
	)
endif()

# Main Quake 2 executable

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	add_executable(yquake2 ${Client-Source} ${Client-Header} ${Platform-Specific-Source}
			${Backends-Generic-Source})
	set_target_properties(yquake2 PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			)
	target_link_libraries(yquake2 ${yquake2LinkerFlags} ${yquake2ClientLinkerFlags}
			${yquake2SDLLinkerFlags} ${yquake2ZLibLinkerFlags} ws2_32 winmm
			${OPENXR_LOADER_LIBRARY} opengl32)
	target_include_directories(yquake2 PRIVATE ${OPENXR_INCLUDE_DIR})

	# Copy the two non-system DLLs next to the binary so the release directory
	# runs on a machine without MSYS2 on PATH.
	get_filename_component(_openxr_libdir ${OPENXR_LOADER_LIBRARY} DIRECTORY)
	get_filename_component(_mingw_bindir "${_openxr_libdir}/../bin" ABSOLUTE)

	# libopenxr_loader.dll is built as C++ and pulls in the MinGW runtime, so
	# those three come along too.
	foreach(_dll SDL2.dll libopenxr_loader.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
		if(EXISTS "${_mingw_bindir}/${_dll}")
			add_custom_command(TARGET yquake2 POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${_mingw_bindir}/${_dll}"
					"${CMAKE_BINARY_DIR}/release/${_dll}")
		endif()
	endforeach()

	# OpenAL is loaded by name at runtime, and DEFAULT_OPENAL_DRIVER on Windows
	# is "openal32.dll". MSYS2 ships the same library as libopenal-1.dll, so it
	# is copied under the name the engine asks for. Without this the load fails
	# and the game runs silent.
	if(EXISTS "${_mingw_bindir}/libopenal-1.dll")
		add_custom_command(TARGET yquake2 POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_mingw_bindir}/libopenal-1.dll"
				"${CMAKE_BINARY_DIR}/release/openal32.dll")
	endif()

	# Wrapper for the Windows binary
	add_executable(quake2 ${Wrapper-Source})
	set_target_properties(quake2 PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			)
else()
	add_executable(quake2 ${Client-Source} ${Client-Header} ${Platform-Specific-Source}
			${Backends-Generic-Source})
	set_target_properties(quake2 PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			)
	target_link_libraries(quake2 ${yquake2LinkerFlags} ${yquake2ClientLinkerFlags}
			${yquake2SDLLinkerFlags} ${yquake2ZLibLinkerFlags})
endif()

# Quake 2 Dedicated Server
# The VR stubs are here because Team Beef's changes call the VR layer from
# frame.c and sv_game.c, both of which the dedicated server compiles. See
# src/vr/vr_stubs_dedicated.c.
add_executable(q2ded ${Server-Source} ${Server-Header} ${Platform-Specific-Source}
		${Backends-Generic-Source} ${VR_SRC_DIR}/vr_stubs_dedicated.c)
set_target_properties(q2ded PROPERTIES
	COMPILE_DEFINITIONS "DEDICATED_ONLY"
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
	)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_link_libraries(q2ded ${yquake2LinkerFlags} ${yquake2SDLLinkerFlags} ${yquake2ZLibLinkerFlags} ws2_32 winmm)
else()
	target_link_libraries(q2ded ${yquake2LinkerFlags} ${yquake2ServerLinkerFlags} ${yquake2ZLibLinkerFlags})
endif()

# Build the game dynamic library
add_library(game MODULE ${Game-Source} ${Game-Header})
set_target_properties(game PROPERTIES
		PREFIX ""
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release/baseq2
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release/baseq2
		SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
		)
target_link_libraries(game ${yquake2LinkerFlags})

# Build the GL1 dynamic library
add_library(ref_gl1 MODULE ${GL1-Source} ${GL1-Header} ${REF-Platform-Specific-Source})
set_target_properties(ref_gl1 PROPERTIES
		PREFIX ""
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
		)
target_link_libraries(ref_gl1 ${yquake2LinkerFlags} ${yquake2OpenGLLinkerFlags} ${yquake2SDLLinkerFlags})

# Build the GL3 dynamic library
#
# Off by default. Quake2Quest ships gl1 only, and their gl3 changes were never
# made to compile: GL3_Init still has the old no-argument signature the rest of
# their tree moved away from, and gl3_mesh.c calls qglScalef, which is a
# fixed-function entry point gl3 does not have. Android only ever built gl1, so
# neither was caught. Finishing gl3 would mean writing weapon-scaling code they
# never wrote, for a renderer their VR path does not use, so it stays disabled
# until the port is complete. gl1 carries both the VR and the flatscreen path.
option(GL3_SUPPORT "Build the GL3 renderer (incomplete in the VR tree)" OFF)

if(GL3_SUPPORT)
	add_library(ref_gl3 MODULE ${GL3-Source} ${GL3-Header} ${REF-Platform-Specific-Source})
	set_target_properties(ref_gl3 PROPERTIES
			PREFIX ""
			LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
			)
	target_link_libraries(ref_gl3 ${yquake2LinkerFlags} ${yquake2SDLLinkerFlags})
endif()

# Build the soft renderer dynamic library
add_library(ref_soft MODULE ${SOFT-Source} ${SOFT-Header} ${REF-Platform-Specific-Source})
set_target_properties(ref_soft PROPERTIES
		PREFIX ""
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
		)
target_link_libraries(ref_soft ${yquake2LinkerFlags} ${yquake2SDLLinkerFlags})

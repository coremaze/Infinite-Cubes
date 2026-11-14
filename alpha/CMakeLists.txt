cmake_minimum_required(VERSION 3.20)
project(InfiniteCubesAlpha LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
 message(FATAL_ERROR "Use a Windows x86 compiler (see README.md)")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExpandSQLite.py"
 "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/sqlite3.c.xz" "${CMAKE_CURRENT_BINARY_DIR}/sqlite3.c"
 COMMAND_ERROR_IS_FATAL ANY)
add_library(sqlite STATIC
 ${CMAKE_CURRENT_BINARY_DIR}/sqlite3.c
)
target_include_directories(sqlite PUBLIC thirdparty/sqlite/.)
add_library(libogg STATIC
 thirdparty/libogg/src/framing.c
 thirdparty/libogg/src/bitwise.c
)
target_include_directories(libogg PUBLIC thirdparty/libogg/include)
add_library(libvorbis STATIC
 thirdparty/libvorbis/lib/analysis.c
 thirdparty/libvorbis/lib/bitrate.c
 thirdparty/libvorbis/lib/block.c
 thirdparty/libvorbis/lib/codebook.c
 thirdparty/libvorbis/lib/envelope.c
 thirdparty/libvorbis/lib/floor0.c
 thirdparty/libvorbis/lib/floor1.c
 thirdparty/libvorbis/lib/info.c
 thirdparty/libvorbis/lib/lookup.c
 thirdparty/libvorbis/lib/lpc.c
 thirdparty/libvorbis/lib/lsp.c
 thirdparty/libvorbis/lib/mapping0.c
 thirdparty/libvorbis/lib/mdct.c
 thirdparty/libvorbis/lib/psy.c
 thirdparty/libvorbis/lib/registry.c
 thirdparty/libvorbis/lib/res0.c
 thirdparty/libvorbis/lib/sharedbook.c
 thirdparty/libvorbis/lib/smallft.c
 thirdparty/libvorbis/lib/synthesis.c
 thirdparty/libvorbis/lib/vorbisenc.c
 thirdparty/libvorbis/lib/window.c
)
target_include_directories(libvorbis PUBLIC thirdparty/libvorbis/include)
target_include_directories(libvorbis PRIVATE thirdparty/libogg/include thirdparty/libvorbis/lib)
add_library(vorbisfile STATIC
 thirdparty/libvorbis/lib/vorbisfile.c
)
target_include_directories(vorbisfile PUBLIC thirdparty/libvorbis/include)
target_include_directories(vorbisfile PRIVATE thirdparty/libogg/include thirdparty/libvorbis/lib)
add_library(freetype STATIC
 thirdparty/freetype/src/autofit/autofit.c
 thirdparty/freetype/src/bdf/bdf.c
 thirdparty/freetype/src/cff/cff.c
 thirdparty/freetype/src/base/ftbase.c
 thirdparty/freetype/src/base/ftbitmap.c
 thirdparty/freetype/src/cache/ftcache.c
 thirdparty/freetype/builds/win32/ftdebug.c
 thirdparty/freetype/src/base/ftfstype.c
 thirdparty/freetype/src/base/ftgasp.c
 thirdparty/freetype/src/base/ftglyph.c
 thirdparty/freetype/src/gzip/ftgzip.c
 thirdparty/freetype/src/base/ftinit.c
 thirdparty/freetype/src/lzw/ftlzw.c
 thirdparty/freetype/src/base/ftstroke.c
 thirdparty/freetype/src/base/ftsystem.c
 thirdparty/freetype/src/smooth/smooth.c
 thirdparty/freetype/src/base/ftbbox.c
 thirdparty/freetype/src/base/ftmm.c
 thirdparty/freetype/src/base/ftpfr.c
 thirdparty/freetype/src/base/ftsynth.c
 thirdparty/freetype/src/base/fttype1.c
 thirdparty/freetype/src/base/ftwinfnt.c
 thirdparty/freetype/src/base/ftxf86.c
 thirdparty/freetype/src/base/ftlcdfil.c
 thirdparty/freetype/src/base/ftgxval.c
 thirdparty/freetype/src/base/ftotval.c
 thirdparty/freetype/src/base/ftpatent.c
 thirdparty/freetype/src/pcf/pcf.c
 thirdparty/freetype/src/pfr/pfr.c
 thirdparty/freetype/src/psaux/psaux.c
 thirdparty/freetype/src/pshinter/pshinter.c
 thirdparty/freetype/src/psnames/psmodule.c
 thirdparty/freetype/src/raster/raster.c
 thirdparty/freetype/src/sfnt/sfnt.c
 thirdparty/freetype/src/truetype/truetype.c
 thirdparty/freetype/src/type1/type1.c
 thirdparty/freetype/src/cid/type1cid.c
 thirdparty/freetype/src/type42/type42.c
 thirdparty/freetype/src/winfonts/winfnt.c
)
target_include_directories(freetype PUBLIC thirdparty/freetype/include)
add_library(pugixml STATIC
 thirdparty/pugixml/src/pugixml.cpp
)
target_include_directories(pugixml PUBLIC thirdparty/pugixml/src)
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)
target_compile_definitions(pugixml PUBLIC PUGIXML_WCHAR_MODE)
target_link_libraries(vorbisfile PUBLIC libvorbis libogg)
include(FetchContent)
FetchContent_Declare(zlib_source URL https://zlib.net/fossils/zlib-1.3.1.tar.gz
 URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
FetchContent_GetProperties(zlib_source)
if(NOT zlib_source_POPULATED)
 FetchContent_Populate(zlib_source)
endif()
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${zlib_source_SOURCE_DIR}" "${zlib_source_BINARY_DIR}" EXCLUDE_FROM_ALL)
target_compile_definitions(zlibstatic PUBLIC Z_PREFIX)
target_compile_definitions(zlibstatic PRIVATE z_errmsg=alpha_z_errmsg)
target_include_directories(zlibstatic PUBLIC "${zlib_source_SOURCE_DIR}" "${zlib_source_BINARY_DIR}")
include(cmake/FreeImage.cmake)
target_include_directories(FreeImage INTERFACE "${FI}/Source")
target_compile_definitions(FreeImage INTERFACE FREEIMAGE_LIB)
add_executable(Cube WIN32
 src/abstr/Machine.cpp
 src/cube/widgets/AdaptionWidget.cpp
 src/cube/Animation.cpp
 src/cube/widgets/CharacterPreviewWidget.cpp
 src/cube/widgets/CharacterStyleWidget.cpp
 src/cube/CombatBehavior.cpp
 src/cube/widgets/BlueprintPreviewWidget.cpp
 src/cube/widgets/CharacterWidget.cpp
 src/cube/widgets/ChatWidget.cpp
 src/cube/CompanionBehavior.cpp
 src/cube/Controller.cpp
 src/cube/Creature.cpp
 src/cube/CreatureStats.cpp
 src/cube/CubeShader.cpp
 src/cube/Database.cpp
 src/cube/Serializer.cpp
 src/cube/widgets/EnchantWidget.cpp
 src/cube/Field.cpp
 src/Thread.cpp
 src/cube/GameController.cpp
 src/cube/GameControllerMovement.cpp
 src/cube/GameControllerRender.cpp
 src/cube/GameControllerShading.cpp
 src/cube/widgets/InventoryWidget.cpp
 src/cube/Item.cpp
 src/cube/LookAtPlayerBehavior.cpp
 src/main.cpp
 src/cube/widgets/MapOverlayWidget.cpp
 src/cube/Network.cpp
 src/cube/widgets/ObjectiveWidget.cpp
 src/cube/Options.cpp
 src/cube/widgets/OptionsWidget.cpp
 src/cube/widgets/PreviewWidget.cpp
 src/cube/QuestText.cpp
 src/cube/RandomInteractionBehavior.cpp
 src/cube/RandomWalkBehavior.cpp
 src/cube/SequentialBehavior.cpp
 src/cube/Server.cpp
 src/cube/widgets/SkillWidget.cpp
 src/cube/SpawnLocationBehavior.cpp
 src/cube/Speech.cpp
 src/cube/widgets/SpeechWidget.cpp
 src/cube/Sprite.cpp
 src/cube/SpriteManager.cpp
 src/cube/widgets/SpriteWidget.cpp
 src/cube/widgets/StartMenuWidget.cpp
 src/cube/widgets/StatisticsWidget.cpp
 src/cube/System.cpp
 src/cube/widgets/SystemWidget.cpp
 src/cube/widgets/VoxelWidget.cpp
 src/cube/WalkPathBehavior.cpp
 src/cube/World.cpp
 src/cube/WorldAI.cpp
 src/cube/WorldCamp.cpp
 src/cube/Spawn.cpp
 src/cube/House.cpp
 src/cube/WorldGeneration.cpp
 src/cube/WorldMap.cpp
 src/cube/widgets/WorldPreviewWidget.cpp
 src/cube/WorldUpdate.cpp
 src/cube/XAudio2Engine.cpp
 src/cube/Zone.cpp
 plasma/src/plasma/Attribute.cpp
 plasma/src/plasma/Button.cpp
 plasma/src/plasma/ChunkStreamReader.cpp
 plasma/src/plasma/CurveShape.cpp
 plasma/src/plasma/D3D9Drawing.cpp
 plasma/src/plasma/D3D9Engine.cpp
 plasma/src/plasma/D3D9RenderSurface.cpp
 plasma/src/plasma/D3D9Texture.cpp
 plasma/src/plasma/Display.cpp
 plasma/src/plasma/Drawing.cpp
 plasma/src/plasma/Edit.cpp
 plasma/src/plasma/Engine.cpp
 plasma/src/plasma/Font.cpp
 plasma/src/plasma/FontEngine.cpp
 plasma/src/plasma/GenericShape.cpp
 plasma/src/plasma/Keyable.cpp
 plasma/src/plasma/ListWidget.cpp
 plasma/src/plasma/MeshShape.cpp
 plasma/src/plasma/Movie.cpp
 plasma/src/plasma/Node.cpp
 plasma/src/plasma/Object.cpp
 plasma/src/plasma/PLXStreamReader.cpp
 plasma/src/plasma/PixelFont.cpp
 plasma/src/plasma/PlasmaFont.cpp
 plasma/src/plasma/PopUpButton.cpp
 plasma/src/plasma/QuadMesh.cpp
 plasma/src/plasma/RenderSurface.cpp
 plasma/src/plasma/ScalableFont.cpp
 plasma/src/plasma/ScrollButton.cpp
 plasma/src/plasma/ScrollSlider.cpp
 plasma/src/plasma/Sequence.cpp
 plasma/src/plasma/Shape.cpp
 plasma/src/plasma/SmoothMeshShape.cpp
 plasma/src/plasma/StaticMeshShape.cpp
 plasma/src/plasma/TextShape.cpp
 plasma/src/plasma/Texture.cpp
 plasma/src/plasma/Transformation.cpp
 plasma/src/plasma/Widget.cpp
)
target_include_directories(Cube PRIVATE include plasma/include platform)
target_compile_definitions(Cube PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS)
option(CUBE_SCRIPTING "The interface script engine (abstr::Machine, cube::System), which runs db.dat's check at start" ON)
if(CUBE_SCRIPTING)
 target_compile_definitions(Cube PRIVATE CUBE_SCRIPTING=1)
else()
 target_compile_definitions(Cube PRIVATE CUBE_SCRIPTING=0)
endif()
target_link_libraries(Cube PRIVATE pugixml sqlite zlibstatic d3d9 dinput8 dxguid winmm ws2_32 iphlpapi glu32 gdi32 ole32 shell32)
add_executable(Server
 src/cube/Animation.cpp
 src/cube/CombatBehavior.cpp
 src/cube/CompanionBehavior.cpp
 src/cube/Creature.cpp
 src/cube/CreatureStats.cpp
 src/cube/CubeShader.cpp
 src/cube/Database.cpp
 src/cube/Field.cpp
 src/Thread.cpp
 src/cube/Item.cpp
 src/cube/LookAtPlayerBehavior.cpp
 src/cube/Network.cpp
 src/cube/QuestText.cpp
 src/cube/RandomInteractionBehavior.cpp
 src/cube/RandomWalkBehavior.cpp
 src/cube/SequentialBehavior.cpp
 src/cube/Serializer.cpp
 src/cube/Server.cpp
 src/cube/SpawnLocationBehavior.cpp
 src/cube/Speech.cpp
 src/cube/Sprite.cpp
 src/cube/SpriteManager.cpp
 src/cube/WalkPathBehavior.cpp
 src/cube/World.cpp
 src/cube/WorldAI.cpp
 src/cube/WorldCamp.cpp
 src/cube/Spawn.cpp
 src/cube/House.cpp
 src/cube/WorldGeneration.cpp
 src/cube/WorldUpdate.cpp
 src/cube/Zone.cpp
 src/server/main.cpp
)
target_include_directories(Server PRIVATE include plasma/include)
target_compile_definitions(Server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Server PRIVATE pugixml sqlite zlibstatic d3d9 dinput8 dxguid winmm ws2_32 iphlpapi glu32 gdi32 ole32 shell32)
target_sources(Cube PRIVATE platform/FreeImageRuntime.cpp)
target_compile_definitions(Server PRIVATE CUBE_SERVER)
target_link_libraries(Cube PRIVATE freetype vorbisfile FreeImage)
foreach(app Cube Server)
 if(MINGW)
  # SSE2 floating point as the original (VC11's /arch:SSE2 /fp:precise): no x87 extended precision,
  # no fused multiply-add contraction.
  target_compile_options(${app} PRIVATE -fwrapv -Wa,-mbig-obj -msse2 -mfpmath=sse -ffp-contract=off)
  target_link_options(${app} PRIVATE -static -static-libgcc -static-libstdc++)
 else()
  target_compile_options(${app} PRIVATE /std:c++17 /bigobj /EHsc /fp:precise)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
   target_compile_options(${app} PRIVATE /arch:SSE2)
  endif()
 endif()
endforeach()
include(cmake/Shaders.cmake)


if(MINGW)
 # Avoid costly inlining of the large sprite registration table.
 set_source_files_properties(src/cube/SpriteManager.cpp PROPERTIES COMPILE_OPTIONS -O1)
endif()

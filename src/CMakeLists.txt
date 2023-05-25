cmake_policy(SET CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Release)

find_package(Threads)
find_package(Vulkan REQUIRED)
find_package(ncnn REQUIRED)

find_program(
  GLSLANGVALIDATOR_EXECUTABLE
  NAMES glslangValidator
  PATHS $ENV{VULKAN_SDK}/bin
  NO_CMAKE_FIND_ROOT_PATH)
message(STATUS "Found glslangValidator: ${GLSLANGVALIDATOR_EXECUTABLE}")

macro(compile_shader SHADER_SRC)
  set(SHADER_SRC_FULLPATH ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SRC})

  get_filename_component(SHADER_SRC_NAME_WE ${SHADER_SRC} NAME_WE)
  set(SHADER_SPV_HEX_FILE
      ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_SRC_NAME_WE}.spv.hex.h)
  add_custom_command(
    OUTPUT ${SHADER_SPV_HEX_FILE}
    COMMAND ${GLSLANGVALIDATOR_EXECUTABLE} ARGS -V -s -x -o
            ${SHADER_SPV_HEX_FILE} ${SHADER_SRC_FULLPATH}
    DEPENDS ${SHADER_SRC_FULLPATH}
    COMMENT "Building SPIR-V module ${SHADER_SRC_NAME_WE}.spv"
    VERBATIM)
  set_source_files_properties(${SHADER_SPV_HEX_FILE} PROPERTIES GENERATED TRUE)
  list(APPEND SHADER_SPV_HEX_FILES ${SHADER_SPV_HEX_FILE})

  # fp16 storage
  set(SHADER_fp16s_SRC_NAME_WE "${SHADER_SRC_NAME_WE}_fp16s")

  set(SHADER_fp16s_SPV_HEX_FILE
      ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_fp16s_SRC_NAME_WE}.spv.hex.h)
  add_custom_command(
    OUTPUT ${SHADER_fp16s_SPV_HEX_FILE}
    COMMAND ${GLSLANGVALIDATOR_EXECUTABLE} ARGS -DNCNN_fp16_storage=1 -V -s -x
            -o ${SHADER_fp16s_SPV_HEX_FILE} ${SHADER_SRC_FULLPATH}
    DEPENDS ${SHADER_SRC_FULLPATH}
    COMMENT "Building SPIR-V module ${SHADER_fp16s_SRC_NAME_WE}.spv"
    VERBATIM)
  set_source_files_properties(${SHADER_fp16s_SPV_HEX_FILE} PROPERTIES GENERATED
                                                                      TRUE)
  list(APPEND SHADER_SPV_HEX_FILES ${SHADER_fp16s_SPV_HEX_FILE})

  # int8 storage
  set(SHADER_int8s_SRC_NAME_WE "${SHADER_SRC_NAME_WE}_int8s")

  set(SHADER_int8s_SPV_HEX_FILE
      ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_int8s_SRC_NAME_WE}.spv.hex.h)
  add_custom_command(
    OUTPUT ${SHADER_int8s_SPV_HEX_FILE}
    COMMAND
      ${GLSLANGVALIDATOR_EXECUTABLE} ARGS -DNCNN_fp16_storage=1
      -DNCNN_int8_storage=1 -V -s -x -o ${SHADER_int8s_SPV_HEX_FILE}
      ${SHADER_SRC_FULLPATH}
    DEPENDS ${SHADER_SRC_FULLPATH}
    COMMENT "Building SPIR-V module ${SHADER_int8s_SRC_NAME_WE}.spv"
    VERBATIM)
  set_source_files_properties(${SHADER_int8s_SPV_HEX_FILE} PROPERTIES GENERATED
                                                                      TRUE)
  list(APPEND SHADER_SPV_HEX_FILES ${SHADER_int8s_SPV_HEX_FILE})
endmacro()

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# enable global link time optimization
cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_supported_output)
if(ipo_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
  message(WARNING "IPO is not supported: ${ipo_supported_output}")
endif()

# look for vulkan compute shader and compile
set(SHADER_SPV_HEX_FILES)

compile_shader(realesrgan_preproc.comp)
compile_shader(realesrgan_postproc.comp)
compile_shader(realesrgan_preproc_tta.comp)
compile_shader(realesrgan_postproc_tta.comp)

add_custom_target(generate-spirv DEPENDS ${SHADER_SPV_HEX_FILES})

add_library(realesrgan main.cpp realesrgan.cpp)
add_dependencies(realesrgan generate-spirv)

set(REALESRGAN_LINK_LIBRARIES ncnn ${Vulkan_LIBRARY})

if(USE_STATIC_MOLTENVK)
  find_library(Cocoa NAMES Cocoa)
  find_library(CoreFoundation NAMES CoreFoundation)
  find_library(CoreGraphics NAMES CoreGraphics)
  find_library(Foundation NAMES Foundation)
  find_library(IOKit NAMES IOKit)
  find_library(IOSurface NAMES IOSurface)
  find_library(Metal NAMES Metal)
  find_library(QuartzCore NAMES QuartzCore)

  list(
    APPEND
    REALESRGAN_LINK_LIBRARIES
    ${Cocoa}
    ${CoreFoundation}
    ${CoreGraphics}
    ${Foundation}
    ${IOKit}
    ${IOSurface}
    ${Metal}
    ${QuartzCore})
endif()

# check VapourSynth headers
if(UNIX)
  set(VAPOURSYNTH_INCLUDE_DIR
      "/usr/local/include/vapoursynth"
      CACHE PATH "VapourSynth header files")
  if(NOT EXISTS "${VAPOURSYNTH_INCLUDE_DIR}/VSHelper.h")
    message(
      FATAL_ERROR
        "VapourSynth header files not found, specify with -DVAPOURSYNTH_INCLUDE_DIR"
    )
  endif()
elseif(WIN32)
  set(VAPOURSYNTH_INCLUDE_DIR
      "C:/Program Files/VapourSynth/sdk/include/vapoursynth"
      CACHE PATH "VapourSynth header files")
  if(NOT EXISTS "${VAPOURSYNTH_INCLUDE_DIR}/VSHelper.h")
    message(
      FATAL_ERROR
        "VapourSynth header files not found, specify with -DVAPOURSYNTH_INCLUDE_DIR"
    )
  endif()
endif()

target_include_directories(realesrgan PRIVATE ${VAPOURSYNTH_INCLUDE_DIR})
target_link_libraries(realesrgan ${REALESRGAN_LINK_LIBRARIES} -static-libstdc++)

install(TARGETS realesrgan LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/vapoursynth)

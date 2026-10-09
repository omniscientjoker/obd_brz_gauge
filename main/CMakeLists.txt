file(GLOB_RECURSE SOURCES_C *.c)
file(GLOB_RECURSE SOURCES_CPP *.cpp)

# ---- UI theme table codegen ------------------------------------------------
# themes/registry.txt + themes/*/<id>/theme.toml -> export_path/ui_theme_generated.c
# Runs BEFORE idf_component_register because SRC_DIRS is globbed at register time.
# Adding a theme requires editing registry.txt, and registry.txt is watched
# below, so a new theme always triggers a reconfigure and regeneration.
set(THEMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../themes")
file(GLOB THEME_MANIFESTS "${THEMES_DIR}/*/*/theme.toml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${THEMES_DIR}/registry.txt" ${THEME_MANIFESTS})

# idf.py sets `python` to the IDF virtualenv interpreter; fall back for plain
# cmake / IDE reconfigures. gen_themes.py has no third-party dependencies.
if(NOT DEFINED python)
    set(python "python3")
endif()

execute_process(
    COMMAND "${python}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_themes.py"
    RESULT_VARIABLE THEME_GEN_RESULT
    OUTPUT_VARIABLE THEME_GEN_STDOUT
    ERROR_VARIABLE  THEME_GEN_STDERR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT THEME_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR
            "Theme generation failed.\n${THEME_GEN_STDOUT}\n${THEME_GEN_STDERR}\n"
            "See themes/README.md.")
endif()
if(THEME_GEN_STDERR)
    message(WARNING "${THEME_GEN_STDERR}")
endif()
message(STATUS "${THEME_GEN_STDOUT}")

# Build metadata for app-side compatibility checks and on-device manifests.
set(OBD_GAUGE_GIT_HASH "unknown")
set(OBD_GAUGE_GIT_BRANCH "unknown")
set(OBD_GAUGE_GIT_COUNT 0)

execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/.." rev-parse --short=12 HEAD
    RESULT_VARIABLE OBD_GAUGE_GIT_HASH_RESULT
    OUTPUT_VARIABLE OBD_GAUGE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT OBD_GAUGE_GIT_HASH_RESULT EQUAL 0 OR OBD_GAUGE_GIT_HASH STREQUAL "")
    set(OBD_GAUGE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/.." rev-parse --abbrev-ref HEAD
    RESULT_VARIABLE OBD_GAUGE_GIT_BRANCH_RESULT
    OUTPUT_VARIABLE OBD_GAUGE_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT OBD_GAUGE_GIT_BRANCH_RESULT EQUAL 0 OR OBD_GAUGE_GIT_BRANCH STREQUAL "")
    set(OBD_GAUGE_GIT_BRANCH "unknown")
endif()

execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/.." rev-list --count HEAD
    RESULT_VARIABLE OBD_GAUGE_GIT_COUNT_RESULT
    OUTPUT_VARIABLE OBD_GAUGE_GIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT OBD_GAUGE_GIT_COUNT_RESULT EQUAL 0 OR OBD_GAUGE_GIT_COUNT STREQUAL "")
    set(OBD_GAUGE_GIT_COUNT 0)
endif()

string(REGEX REPLACE "[^A-Za-z0-9._-]" "_" OBD_GAUGE_GIT_BRANCH_SAFE "${OBD_GAUGE_GIT_BRANCH}")
set(OBD_GAUGE_BUILD_TAG "${OBD_GAUGE_GIT_BRANCH_SAFE}-${OBD_GAUGE_GIT_COUNT}-${OBD_GAUGE_GIT_HASH}")

idf_component_register(SRC_DIRS
                    "."
                    "export_path/components"
                    "export_path/fonts"
                    "export_path/images"
                    "export_path/theme_assets"
                    "export_path/screens"
                    "export_path"
                    "app_obd_dsp"
                    "app_media"
                    "bsp_obd_dsp"
                    "bsp_obd_dsp/lcd_driver"
                    "bsp_obd_dsp/lcd_driver/esp_lcd_st77916"
                    "bsp_obd_dsp/exio"
                    "bsp_obd_dsp/i2c_driver"
                    "bsp_obd_dsp/touch_driver"
                    "theme_engine"
                    INCLUDE_DIRS
                    "."
                    REQUIRES
                    esp_http_server
                    esp_wifi
                    bt
                    spiffs
                    fatfs
                    sdmmc
                    esp_driver_i2s
                    esp_codec_dev
                    mbedtls
                    nvs_flash
                    app_update
                    json)

# bootmedia is a raw partition, not a SPIFFS filesystem.  The runtime reads a
# fixed 4 KiB manifest slot followed by the animation stream directly from
# flash, and the OTA upload path writes the same layout.
set(BOOTMEDIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bootmedia/slot_a")
set(BOOTMEDIA_IMAGE "${CMAKE_BINARY_DIR}/bootmedia.bin")
set(BOOTMEDIA_IMAGE_TOOL "${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_bootmedia_image.py")
add_custom_command(
    OUTPUT "${BOOTMEDIA_IMAGE}"
    COMMAND "${python}" "${BOOTMEDIA_IMAGE_TOOL}"
            "${BOOTMEDIA_DIR}/boot_block.txt"
            "${BOOTMEDIA_DIR}/boot_block.bin"
            "${BOOTMEDIA_IMAGE}"
    DEPENDS "${BOOTMEDIA_IMAGE_TOOL}"
            "${BOOTMEDIA_DIR}/boot_block.txt"
            "${BOOTMEDIA_DIR}/boot_block.bin"
    VERBATIM)
add_custom_target(bootmedia_raw_bin ALL DEPENDS "${BOOTMEDIA_IMAGE}")

partition_table_get_partition_info(bootmedia_size "--partition-name bootmedia" "size")
partition_table_get_partition_info(bootmedia_offset "--partition-name bootmedia" "offset")
if(NOT bootmedia_size OR NOT bootmedia_offset)
    message(FATAL_ERROR "Failed to resolve bootmedia partition geometry")
endif()
idf_component_get_property(bootmedia_main_args esptool_py FLASH_ARGS)
idf_component_get_property(bootmedia_sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(bootmedia-flash "${bootmedia_main_args}" "${bootmedia_sub_args}" ALWAYS_PLAINTEXT)
esptool_py_flash_to_partition(bootmedia-flash bootmedia "${BOOTMEDIA_IMAGE}")
add_dependencies(bootmedia-flash bootmedia_raw_bin)
esptool_py_flash_to_partition(flash bootmedia "${BOOTMEDIA_IMAGE}")
add_dependencies(flash bootmedia_raw_bin)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    OBD_GAUGE_GIT_HASH="${OBD_GAUGE_GIT_HASH}"
    OBD_GAUGE_GIT_BRANCH="${OBD_GAUGE_GIT_BRANCH_SAFE}"
    OBD_GAUGE_GIT_COUNT=${OBD_GAUGE_GIT_COUNT}
    OBD_GAUGE_BUILD_TAG="${OBD_GAUGE_BUILD_TAG}")

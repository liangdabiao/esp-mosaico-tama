idf_component_register(
    SRCS "main.c" "game.c" "twp_model.c" "twp_games.c" "twp_data_tables.c"
         "twp_cjk_font.c" "twp_store.c" "twp_sfx.c" "rtttl_player.c"
    INCLUDE_DIRS "."
    REQUIRES mosaico_game_app mosaico_raylib_fast mosaico_game_2d
             mosaico_game_assets esp-gsp nvs_flash
             esp-mosaico-bsp esp_codec_dev)

# Deterministic pixel atlas, packed by the same engine tool the simulator
# drives (game.sim.json asset_prepare). The .atlas is embedded into the app
# binary and registered as a memory asset at startup — no data partition.
set(GAME_ASSET_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../assets_src")
set(GAME_ASSET_OUTPUT "${CMAKE_CURRENT_LIST_DIR}/../assets/generated")
add_custom_target(tamaweb_pet_assets ALL
    COMMAND "${PYTHON}" "${GAME_ASSET_SOURCE}/generate_sprites.py"
    COMMAND "${PYTHON}" "${RAYLIB_LITE_ENGINE_ROOT}/tools/pack_game_assets.py"
            --source "${GAME_ASSET_SOURCE}" --output "${GAME_ASSET_OUTPUT}"
    BYPRODUCTS "${GAME_ASSET_OUTPUT}/tamaweb.atlas"
               "${GAME_ASSET_OUTPUT}/assets_ids.h"
    DEPENDS "${GAME_ASSET_SOURCE}/game_assets.json"
            "${GAME_ASSET_SOURCE}/atlas.json"
            "${GAME_ASSET_SOURCE}/generate_sprites.py")
add_dependencies(${COMPONENT_LIB} tamaweb_pet_assets)
target_add_binary_data(${COMPONENT_LIB} "${GAME_ASSET_OUTPUT}/tamaweb.atlas" BINARY
    RENAME_TO tamaweb_atlas DEPENDS tamaweb_pet_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${GAME_ASSET_OUTPUT}")

# GSP needs an image to bind the Raylib framebuffer. This generated placeholder
# is embedded in the application; no external game resource partition is used.
set(canvas_png "${CMAKE_CURRENT_BINARY_DIR}/canvas.png")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/make_canvas_png.py"
    "${canvas_png}" RESULT_VARIABLE canvas_result)
if(NOT canvas_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the game canvas placeholder")
endif()
set(CANVAS_IMAGE "${canvas_png}")
configure_file("scene.json.in" "${CMAKE_CURRENT_BINARY_DIR}/scene.json" @ONLY)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${CMAKE_CURRENT_BINARY_DIR}/scene.json"
    PIXEL_FORMAT rgb565)

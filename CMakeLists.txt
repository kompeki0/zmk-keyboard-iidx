if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources(
    app PRIVATE
    src/value_store.c
    src/behavior_sensor_iidx_x_value.c
  )
  zephyr_include_directories(include)
endif()

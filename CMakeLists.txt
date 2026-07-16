if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources(
    app PRIVATE
    src/value_store.c
    src/behavior_sensor_iidx_x_value.c
  )

  if(CONFIG_ZMK_IIDX_HID)
    target_sources(
      app PRIVATE
      src/iidx_hid.c
      src/behavior_iidx_boot_select.c
      src/behavior_iidx_button.c
      src/behavior_iidx_mode.c
    )
  endif()

  zephyr_include_directories(include)
endif()

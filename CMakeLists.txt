add_subdirectory(drivers)

zephyr_include_directories(include)

target_sources(app PRIVATE
  src/behaviors/behavior_cirque_mode.c
  src/behaviors/behavior_drag_scroll.c
  src/behaviors/behavior_pointing_speed.c
  src/input_processor_drag_scroll.c
  src/input_processor_pointer_speed.c
)

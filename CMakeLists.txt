add_subdirectory(drivers)

zephyr_include_directories(include)

target_sources(app PRIVATE
  src/behaviors/behavior_drag_scroll.c
  src/behaviors/behavior_pointing_speed.c
  src/behaviors/behavior_sniping.c
  src/input_processor_drag_scroll.c
  src/input_processor_sniping.c
)

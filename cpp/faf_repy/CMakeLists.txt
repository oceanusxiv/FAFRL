set_target_properties(faf_sim zlib123 PROPERTIES POSITION_INDEPENDENT_CODE ON)

nanobind_add_module(faf_repy
    ${CMAKE_CURRENT_SOURCE_DIR}/faf_runtime.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/faf_sim_core.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/faf_sim_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ensure_sim_thread.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ensure_sim_thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/faf_nanobind_module.cpp
)
target_include_directories(faf_repy INTERFACE
    "${CMAKE_SOURCE_DIR}/dependencies/faf-re/src")
target_compile_options(faf_repy PRIVATE -fvisibility=hidden)
target_link_libraries(faf_repy PRIVATE faf_sim)
# We get undefined symbols unless we strip them out here
target_link_options(faf_repy PRIVATE
    -Wl,--gc-sections
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/faf_nb.version
)

nanobind_add_stub(
  faf_repy_stub
  MODULE faf_repy
  MARKER_FILE py.typed
  OUTPUT faf_repy.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:faf_repy>
  DEPENDS faf_repy
)


if(DEFINED SKBUILD_PROJECT_NAME)
    install(TARGETS faf_repy DESTINATION ${SKBUILD_PROJECT_NAME})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/py.typed" "${CMAKE_CURRENT_BINARY_DIR}/faf_repy.pyi" DESTINATION ${SKBUILD_PROJECT_NAME})
endif()

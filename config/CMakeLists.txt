
CONFIGURE_FILE(cynagora.conf.in cynagora.conf @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cynagora.conf DESTINATION ${DEFAULT_CONF_DIR} RENAME ${DEFAULT_CONFIG_BASENAME})

add_subdirectory(cynagora.d)

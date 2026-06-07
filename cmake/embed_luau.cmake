function(petrichor_embed_luau TARGET LUAU_DIR)
    find_package(Python3 REQUIRED)
    get_target_property(PETRICHOR_SOURCE_DIR petrichor SOURCE_DIR)
    set(_BIN2H "${PETRICHOR_SOURCE_DIR}/tools/bin2h.py")
    
    file(GLOB LUAU_SOURCES "${LUAU_DIR}/*.luau")
    set(EMBED_TARGET "embed_luau_${TARGET}")
    add_custom_target(${EMBED_TARGET}
        COMMAND ${Python3_EXECUTABLE} ${_BIN2H}
            ${LUAU_SOURCES} ${CMAKE_CURRENT_BINARY_DIR} luau
        DEPENDS ${LUAU_SOURCES} ${_BIN2H}
    )
    add_dependencies(${TARGET} ${EMBED_TARGET})
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
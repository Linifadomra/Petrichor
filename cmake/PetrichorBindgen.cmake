# cmake/PetrichorBindgen.cmake
# ---------------------------------------------------------------------------
# petrichor_bindgen(
#     TARGET    <name>          # CMake target with AUGMENT_MANIFEST_JSON set
#     OUT_DIR   <path>          # Output directory (files written under here)
#     LANGS     cs luau ...     # One or more emitter languages (default: cs)
#     NAMESPACE <ns>            # Namespace / module root (default: Game)
# )
#
# For each language, runs the bindgen tool and writes generated files into
# OUT_DIR/<lang>/. Creates a <TARGET>_bindgen target and wires it as a
# dependency of TARGET.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(petrichor_bindgen)
    cmake_parse_arguments(
        PBG
        ""
        "TARGET;OUT_DIR;NAMESPACE"
        "LANGS"
        ${ARGN}
    )

    if(NOT PBG_TARGET)
        message(FATAL_ERROR "[petrichor_bindgen] TARGET is required")
    endif()
    if(NOT PBG_OUT_DIR)
        message(FATAL_ERROR "[petrichor_bindgen] OUT_DIR is required")
    endif()
    if(NOT PBG_LANGS)
        set(PBG_LANGS "cs")
    endif()
    if(NOT PBG_NAMESPACE)
        set(PBG_NAMESPACE "Game")
    endif()

    get_target_property(_manifest ${PBG_TARGET} AUGMENT_MANIFEST_JSON)
    if(NOT _manifest)
        message(FATAL_ERROR
            "[petrichor_bindgen] TARGET '${PBG_TARGET}' has no AUGMENT_MANIFEST_JSON property.\n"
            "Call augment_codegen_target() for this target first.")
    endif()

    if(NOT DEFINED petrichor_SOURCE_DIR)
        message(FATAL_ERROR
            "[petrichor_bindgen] petrichor_SOURCE_DIR is not set.\n"
            "Add petrichor via CPMAddPackage or add_subdirectory first.")
    endif()
    set(_bindgen_dir "${petrichor_SOURCE_DIR}/tools/bindgen")

    set(_all_outputs)

    foreach(_lang ${PBG_LANGS})
        set(_out_dir "${PBG_OUT_DIR}/${_lang}")
        set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${PBG_TARGET}_bindgen_${_lang}.stamp")

        add_custom_command(
            OUTPUT  "${_stamp}"
            COMMAND "${Python3_EXECUTABLE}" -m bindgen
                    "--input=${_manifest}"
                    "--lang=${_lang}"
                    "--namespace=${PBG_NAMESPACE}"
                    "--output=${_out_dir}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
            DEPENDS "${_manifest}"
            WORKING_DIRECTORY "${_bindgen_dir}"
            COMMENT "[Petrichor] Generating ${_lang} bindings for ${PBG_TARGET}..."
            VERBATIM
        )

        list(APPEND _all_outputs "${_stamp}")

        message(STATUS
            "[Petrichor] Bindgen: ${PBG_TARGET} -> ${_lang} -> ${_out_dir}")
    endforeach()

    set(_bindgen_target "${PBG_TARGET}_bindgen")
    add_custom_target(${_bindgen_target} DEPENDS ${_all_outputs})
    add_dependencies(${PBG_TARGET} ${_bindgen_target})
endfunction()

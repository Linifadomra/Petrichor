# cmake/PetrichorBindgen.cmake
# ---------------------------------------------------------------------------
# petrichor_bindgen(
#     TARGET    <name>          # CMake target with AUGMENT_MANIFEST_JSON set
#     OUT_DIR   <path>          # Output directory (files written under here)
#     LANGS     cs luau ...     # One or more emitter languages (default: cs)
#     NAMESPACE <ns>            # Namespace / module root (default: Game)
# )
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

function(petrichor_bindgen)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
    set(_bindgen_dir "${petrichor_SOURCE_DIR}/tools/")

    if(NOT EXISTS "${_manifest}")
        message(STATUS "[petrichor_bindgen] symbols.json not found yet, skipping (run augment codegen first)")
        return()
    endif()

    foreach(_lang ${PBG_LANGS})
        set(_out_dir "${PBG_OUT_DIR}/${_lang}")
        message(STATUS "[Petrichor] Generating ${_lang} bindings -> ${_out_dir}")

        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m bindgen
                    "--input=${_manifest}"
                    "--lang=${_lang}"
                    "--namespace=${PBG_NAMESPACE}"
                    "--output=${_out_dir}"
            WORKING_DIRECTORY "${_bindgen_dir}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE  _err
        )

        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "[petrichor_bindgen] bindgen failed for lang '${_lang}':\nstdout: ${_out}\nstderr: ${_err}")
        endif()

    endforeach()

    # Reconfigure if symbols.json changes so bindings stay in sync
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")

    message(STATUS "[Petrichor] Bindgen done for '${PBG_TARGET}'")
endfunction()

execute_process(
    COMMAND xxd -i -n ${NAME} ${INPUT}
    OUTPUT_FILE ${OUTPUT}
)
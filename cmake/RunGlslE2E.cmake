file(MAKE_DIRECTORY ${OUTDIR})

execute_process(COMMAND ${RTSLC} -c ${SHADER} -o ${OUTDIR}/shader.rtslo
    RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "rtslc -c failed (${r}):\n${out}\n${err}")
endif()

execute_process(COMMAND ${RTSLC} --link-program ${OUTDIR}/shader.rtslo -o ${OUTDIR}/shader.rtslp
    RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "rtslc --link-program failed (${r}):\n${out}\n${err}")
endif()

execute_process(COMMAND ${RTSLC} --emit-glsl ${OUTDIR}/shader.rtslp -o ${OUTDIR}/shader
    RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "rtslc --emit-glsl failed (${r}):\n${out}\n${err}")
endif()

file(GLOB stage_files ${OUTDIR}/shader.vert ${OUTDIR}/shader.frag ${OUTDIR}/shader.comp)
if(NOT stage_files)
    message(FATAL_ERROR "no .vert/.frag/.comp emitted")
endif()

foreach(f ${stage_files})
    execute_process(COMMAND ${GLSLANG} -V ${f} -o ${f}.spv
        RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "glslangValidator rejected ${f} (${r}):\n${out}\n${err}")
    endif()
endforeach()

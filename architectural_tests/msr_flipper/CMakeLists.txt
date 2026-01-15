set(KERNEL_SOURCE_DIR "/lib/modules/${CMAKE_SYSTEM_VERSION}/build")

list(APPEND module_sources msr_flipper.c msr_flipper.h)
foreach(src ${module_sources})
  configure_file(${src} ${src} COPYONLY)
endforeach()

set(DRIVER_FILE msr_flipper.ko)
set(KBUILD_CMD make -C ${KERNEL_SOURCE_DIR} modules M=${CMAKE_CURRENT_BINARY_DIR} src=${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(
        OUTPUT ${DRIVER_FILE}
        COMMAND ${KBUILD_CMD}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS msr_flipper.c VERBATIM)

add_custom_target(msr_flipper ALL DEPENDS ${DRIVER_FILE})
add_custom_target(clean-module COMMAND make -C ${KERNEL_SOURCE_DIR} M=${CMAKE_CURRENT_BINARY_DIR} clean)
add_custom_target(module-copy ALL
    COMMAND cp ${CMAKE_BINARY_DIR}/msr_flipper/msr_flipper.ko "${CMAKE_BINARY_DIR}"
    DEPENDS msr_flipper
)
set_property(TARGET module-copy PROPERTY ADDITIONAL_CLEAN_FILES "${CMAKE_BINARY_DIR}/msr_flipper.ko")    

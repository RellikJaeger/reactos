
add_definitions(-D__WINESRC__ -D__ROS_LONG64__)
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/wine)
spec2def(vbscript.dll vbscript.spec)

list(APPEND SOURCE
    compile.c
    global.c
    interp.c
    lex.c
    regexp.c
    utils.c
    vbdisp.c
    vbregexp.c
    vbscript.c
    vbscript_main.c)

list(APPEND PCH_SKIP_SOURCE
    parser.tab.c)

# parser.tab.c has been generated with relative file paths...
set_source_files_properties(parser.tab.c PROPERTIES COMPILE_FLAGS "-UREACTOS_SOURCE_DIR -DREACTOS_SOURCE_DIR=\"\\\".\\\"\"")

list(APPEND vbscript_rc_deps
    ${CMAKE_CURRENT_SOURCE_DIR}/vbscript_classes.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/vbsglobal.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/vbsregexp10.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/vbsregexp55.rgs
    ${CMAKE_CURRENT_BINARY_DIR}/vbsglobal.tlb
    ${CMAKE_CURRENT_BINARY_DIR}/vbsregexp10.tlb
    ${CMAKE_CURRENT_BINARY_DIR}/vbsregexp55.tlb)

set_source_files_properties(vbscript.rc PROPERTIES OBJECT_DEPENDS "${vbscript_rc_deps}")
add_idl_headers(vbscript_idlheader vbscript_classes.idl vbsglobal.idl vbsregexp55.idl)
add_typelib(vbsglobal.idl vbsregexp10.idl vbsregexp55.idl)

add_library(vbscript MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    vbscript.rc
    ${CMAKE_CURRENT_BINARY_DIR}/vbscript.def)

set_module_type(vbscript win32dll)
target_link_libraries(vbscript uuid wine)
add_importlibs(vbscript oleaut32 ole32 user32 msvcrt kernel32 ntdll)
add_dependencies(vbscript vbscript_idlheader stdole2)
add_pch(vbscript precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET vbscript DESTINATION reactos/system32 FOR all)

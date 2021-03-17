set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/../3rdparty/libcurl/curl
    ${CMAKE_CURRENT_BINARY_DIR}/curl EXCLUDE_FROM_ALL)

add_custom_command(OUTPUT urlfetch_u.h urlfetch_u.c urlfetch_args.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../urlfetch.edl
    COMMAND openenclave::oeedger8r
    --search-path ${OE_INCLUDEDIR}
    --search-path ${OE_INCLUDEDIR}/openenclave/edl/sgx
    --untrusted ${CMAKE_CURRENT_SOURCE_DIR}/../urlfetch.edl)

add_executable(urlfetch.host
    host.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/urlfetch_u.c)

# Needed for the generated file urlfetch_u.h
target_include_directories(urlfetch.host PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(urlfetch.host
    openenclave::oehost
    libcurl
    )

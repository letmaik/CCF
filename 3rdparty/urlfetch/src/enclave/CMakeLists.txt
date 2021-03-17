add_custom_command(OUTPUT urlfetch_t.h urlfetch_t.c urlfetch_args.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../urlfetch.edl
    COMMAND openenclave::oeedger8r
    --search-path ${OE_INCLUDEDIR}
    --search-path ${OE_INCLUDEDIR}/openenclave/edl/sgx
    --trusted ${CMAKE_CURRENT_SOURCE_DIR}/../urlfetch.edl)

add_executable(urlfetch.enclave
    enclave.cpp
    curl_helper.cpp
    curl_helper.h
    ${CMAKE_CURRENT_BINARY_DIR}/urlfetch_t.c)

# Needed for the generated file urlfetch_t.h
target_include_directories(urlfetch.enclave PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(urlfetch.enclave
    openenclave-curl::libcurl
    openenclave::oeenclave
    openenclave::oelibc)

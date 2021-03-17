cmake_minimum_required(VERSION 3.13)

project(urlfetch-tests C CXX)

include(CTest)

add_test(NAME urlfetch COMMAND tests.host $<TARGET_FILE:tests.enclave>)

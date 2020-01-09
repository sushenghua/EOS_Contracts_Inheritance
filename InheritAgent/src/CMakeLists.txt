cmake_minimum_required(VERSION 3.16)

project(InheritAgent)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

add_contract( InheritAgent InheritAgent InheritAgent.cpp )
target_include_directories( InheritAgent PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( InheritAgent ${CMAKE_SOURCE_DIR}/../ricardian )
target_compile_definitions( InheritAgent PUBLIC DEBUG DEBUG_PRINT )
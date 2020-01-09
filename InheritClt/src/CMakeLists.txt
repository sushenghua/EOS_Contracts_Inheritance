cmake_minimum_required(VERSION 3.16)

project(InheritClt)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

add_contract( InheritClt InheritClt InheritClt.cpp )
target_include_directories( InheritClt PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( InheritClt ${CMAKE_SOURCE_DIR}/../ricardian )
target_compile_definitions( InheritClt PUBLIC DEBUG DEBUG_PRINT )

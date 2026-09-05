cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBICNS_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_icns.c
)

# libicns can store icon elements as embedded PNG, so its io layer pulls in
# libpng (png_get_io_ptr and friends); without it the side module has an
# unresolved import and never instantiates.
SET(LIBICNS_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libicns.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libpng16.a
)

SET(LIBICNS_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libicns
        "${LIBICNS_SRC}"
        "${LIBICNS_LIBS}"
        []
        ""
        "${LIBICNS_INC}"
        "-s USE_ZLIB=1 -lz"
        "1")

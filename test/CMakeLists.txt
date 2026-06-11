# Pie test executables (standalone)

add_executable(pie_client client.cc)
add_executable(pie_server server.cc)

foreach(_tgt pie_client pie_server)
    target_include_directories(${_tgt} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(${_tgt} PRIVATE
        pie
        Threads::Threads
    )
endforeach()

set(CMAKE_AUTOMOC ON)

add_executable(protocol_tests
    protocol_tests.cpp
)

target_link_libraries(protocol_tests
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        qtlink_protocol
)

add_test(
    NAME protocol_tests
    COMMAND protocol_tests
)